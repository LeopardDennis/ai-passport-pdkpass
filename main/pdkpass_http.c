#include "pdkpass_http.h"
#include "pdkpass_json_stream.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static SemaphoreHandle_t s_transaction;
// Access only by the owner of s_transaction. Zero leaves automatic GETs unchanged.
static int64_t s_deadline_us;
static int64_t s_retry_at_us;
static int64_t s_jolpica_retry_at_us;
// Shared across transactions/services and radio reconnects. OpenF1's free
// limit is 30/minute; 2.1-second spacing leaves headroom below that limit.
// Jolpica permits 500/hour; 7.3-second spacing also meets its burst limit.
static int64_t s_request_at_us[3];
// One TLS connection, owned by the shared transaction, never retained while idle.
static esp_http_client_handle_t s_client;
static unsigned s_client_origin;
static bool s_transaction_active;

void pdkpass_http_release(void)
{
    if (s_client) esp_http_client_cleanup(s_client);
    s_client = NULL; s_client_origin = 0;
}

static unsigned http_origin(const char *url)
{
    if (strncmp(url, "https://api.openf1.org/", sizeof("https://api.openf1.org/") - 1) == 0) return 1;
    if (strncmp(url, "https://api.jolpi.ca/", sizeof("https://api.jolpi.ca/") - 1) == 0) return 2;
    return 0;
}

static const char *TAG = "pdk_http";
typedef struct {
    char *data;
    size_t length, capacity, limit;
    esp_err_t error;
    pdkpass_json_stream_t *stream;
    pdkpass_http_item_fn item;
    void *context;
    unsigned retry_seconds;
    const char *failure_stage;
} response_t;

static void report_failure(const char *stage, int status, esp_err_t err,
                           size_t bytes)
{
    ESP_LOGW(TAG, "GET stage=%s status=%d err=%s bytes=%u",
             stage, status, esp_err_to_name(err), (unsigned)bytes);
}

void pdkpass_http_report_data_failure(const char *stage, esp_err_t err,
                                      size_t bytes)
{
    report_failure(stage, err == ESP_ERR_NO_MEM ? 0 : 200, err, bytes);
}

esp_err_t pdkpass_http_init(void)
{
    if (!s_transaction) s_transaction = xSemaphoreCreateMutex();
    return s_transaction ? ESP_OK : ESP_ERR_NO_MEM;
}
void pdkpass_http_begin(void) { xSemaphoreTake(s_transaction, portMAX_DELAY); s_transaction_active = true; }
bool pdkpass_http_try_begin(void) {
    if (!s_transaction || xSemaphoreTake(s_transaction, 0) != pdTRUE) return false;
    s_transaction_active = true; return true;
}
bool pdkpass_http_expired(void)
{
    return s_deadline_us > 0 && esp_timer_get_time() >= s_deadline_us;
}

bool pdkpass_http_begin_until(int64_t deadline_us)
{
    int64_t remaining = deadline_us - esp_timer_get_time();
    if (!s_transaction || remaining <= 0) return false;
    TickType_t ticks = pdMS_TO_TICKS((remaining + 999) / 1000);
    if (xSemaphoreTake(s_transaction, ticks ? ticks : 1) != pdTRUE) return false;
    if (esp_timer_get_time() >= deadline_us) {
        xSemaphoreGive(s_transaction);
        return false;
    }
    s_transaction_active = true;
    s_deadline_us = deadline_us;
    return true;
}

void pdkpass_http_end(void)
{
    pdkpass_http_release();
    s_transaction_active = false;
    s_deadline_us = 0;
    xSemaphoreGive(s_transaction);
}

static bool stream_item(const char *json, void *context)
{
    response_t *response = context;
    const char *end = NULL;
    cJSON *object = cJSON_ParseWithOpts(json, &end, true);
    bool ok = cJSON_IsObject(object);
    if (!ok) response->failure_stage = "json-item";
    else if (!response->item(object, response->context)) {
        response->failure_stage = "item-rejected";
        ok = false;
    }
    cJSON_Delete(object);
    return ok;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (!r) return ESP_OK;
    if ((event->event_id == HTTP_EVENT_ON_HEADER || event->event_id == HTTP_EVENT_ON_DATA) &&
        pdkpass_http_expired()) {
        r->failure_stage = "operation-timeout";
        r->error = ESP_ERR_TIMEOUT;
        // IDF ignores ON_DATA callback return values. Close the transport from
        // its owning task to stop a response that keeps trickling data.
        esp_http_client_close(event->client);
        return r->error;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        event->header_value &&
        strcasecmp(event->header_key, "Retry-After") == 0) {
        char *end;
        unsigned long seconds = strtoul(event->header_value, &end, 10);
        if (end != event->header_value && *end == '\0') {
            r->retry_seconds = seconds > 86400UL ? 86400U : (unsigned)seconds;
        }
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if (r->error != ESP_OK) return r->error;
    size_t count = (size_t)event->data_len;
    // Do not feed an error page into the JSON consumer.
    if (esp_http_client_get_status_code(event->client) != 200) return ESP_OK;
    if (count > r->limit - r->length) {
        r->failure_stage = "body-limit";
        return r->error = ESP_ERR_INVALID_SIZE;
    }
    if (r->stream) {
        r->length += count;
        if (!pdkpass_json_stream_feed(r->stream, event->data, count)) {
            if (!r->failure_stage) r->failure_stage = "json-stream";
            return r->error = ESP_ERR_INVALID_RESPONSE;
        }
        return ESP_OK;
    }
    size_t needed = r->length + count + 1U;
    if (needed > r->capacity) {
        size_t capacity = r->capacity ? r->capacity : 2048;
        while (capacity < needed && capacity < r->limit + 1U) capacity *= 2U;
        if (capacity > r->limit + 1U) capacity = r->limit + 1U;
        char *data = realloc(r->data, capacity);
        if (!data) {
            r->failure_stage = "body-alloc";
            return r->error = ESP_ERR_NO_MEM;
        }
        r->data = data;
        r->capacity = capacity;
    }
    memcpy(r->data + r->length, event->data, count);
    r->length += count;
    r->data[r->length] = '\0';
    return ESP_OK;
}

static bool wait_for_request(unsigned origin, int64_t retry_at)
{
    int64_t now = esp_timer_get_time();
    // Automatic refreshes defer server cooldown to their scheduler. Manual
    // refreshes may wait, within their original deadline, instead of failing
    // immediately when another worker just encountered a rate limit.
    if (!s_deadline_us && now < retry_at) return false;
    int64_t ready = retry_at;
    if (origin && s_request_at_us[origin] > ready) ready = s_request_at_us[origin];
    if (pdkpass_http_expired() || (s_deadline_us > 0 && ready >= s_deadline_us)) return false;
    while (now < ready) {
        int64_t remaining_ms = (ready - now + 999) / 1000;
        if (remaining_ms > 1000) remaining_ms = 1000;
        TickType_t ticks = pdMS_TO_TICKS(remaining_ms);
        vTaskDelay(ticks ? ticks : 1);
        if (pdkpass_http_expired()) return false;
        now = esp_timer_get_time();
    }
    if (origin) s_request_at_us[origin] = now + (origin == 1 ? 2100000LL : 7300000LL);
    return true;
}

static esp_err_t perform(const char *url, response_t *r)
{
    int64_t *retry_at = strncmp(url, "https://api.jolpi.ca/", sizeof("https://api.jolpi.ca/") - 1U) == 0
                            ? &s_jolpica_retry_at_us : &s_retry_at_us;
    unsigned origin = http_origin(url);
    if (!wait_for_request(origin, *retry_at)) {
        report_failure(pdkpass_http_expired() ? "operation-timeout" : "request-wait",
                       0, ESP_ERR_TIMEOUT, 0);
        return ESP_ERR_TIMEOUT;
    }
    esp_http_client_config_t config = {
        .url = url, .event_handler = http_event, .user_data = r,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000,
        .buffer_size = 1024, .user_agent = "PDKPASS/1.1",
        .is_async = s_deadline_us > 0,
        .keep_alive_enable = true,
    };
    bool reusable = s_transaction_active && origin != 0;
    if (s_client && (!reusable || origin != s_client_origin)) pdkpass_http_release();
    esp_http_client_handle_t client = s_client;
    if (client) {
        esp_err_t setup = esp_http_client_set_url(client, url);
        if (setup == ESP_OK) setup = esp_http_client_set_user_data(client, r);
        if (setup != ESP_OK) { pdkpass_http_release(); return setup; }
        esp_http_client_set_timeout_ms(client, 15000);
    } else {
        client = esp_http_client_init(&config);
        if (client && reusable) { s_client = client; s_client_origin = origin; }
    }
    if (!client) {
        report_failure("client-init", 0, ESP_ERR_NO_MEM, 0);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t transport_err;
    do {
        if (pdkpass_http_expired()) {
            r->failure_stage = "operation-timeout";
            transport_err = ESP_ERR_TIMEOUT;
            break;
        }
        if (s_deadline_us > 0) {
            int64_t remaining_ms = (s_deadline_us - esp_timer_get_time() + 999) / 1000;
            if (remaining_ms < 1) remaining_ms = 1;
            esp_http_client_set_timeout_ms(client, remaining_ms < 15000 ? (int)remaining_ms : 15000);
        }
        transport_err = esp_http_client_perform(client);
        if (transport_err == ESP_ERR_HTTP_EAGAIN && r->error == ESP_OK)
            vTaskDelay(1);
    } while (transport_err == ESP_ERR_HTTP_EAGAIN && r->error == ESP_OK);
    if (pdkpass_http_expired()) {
        r->failure_stage = "operation-timeout";
        r->error = ESP_ERR_TIMEOUT;
    }
    int status = esp_http_client_get_status_code(client);
    esp_err_t err = r->error != ESP_OK ? r->error : transport_err;
    if (err == ESP_OK && (status != 200 || r->length == 0)) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && r->stream && !pdkpass_json_stream_done(r->stream))
        err = ESP_ERR_INVALID_RESPONSE;
    // Callback contexts are stack-owned. Cleanup may emit DISCONNECTED later.
    esp_http_client_set_user_data(client, NULL);
    if (client == s_client) {
        if (err != ESP_OK) pdkpass_http_release();
    } else esp_http_client_cleanup(client);
    if (status == 429 || status == 503) {
        unsigned seconds = r->retry_seconds ? r->retry_seconds : 60;
        *retry_at = esp_timer_get_time() + (int64_t)seconds * 1000000LL;
    }
    if (err != ESP_OK) {
        const char *stage = r->failure_stage ? r->failure_stage
                            : transport_err != ESP_OK ? "transport"
                            : status != 200 ? "http-status"
                            : r->length == 0 ? "empty-body" : "stream-end";
        // Identify the API resource without logging query parameters, which
        // may contain device- or user-specific values in future callers.
        const char *endpoint = strstr(url, "/v1/");
        if (endpoint) {
            endpoint += 4;
            size_t length = strcspn(endpoint, "?");
            if (length > 0 && length < 32)
                ESP_LOGW(TAG, "GET endpoint=%.*s", (int)length, endpoint);
        }
        report_failure(stage, status, err, r->length);
    }
    return err;
}

esp_err_t pdkpass_http_get(const char *url, size_t limit, char **json)
{
    if (!url || !json || limit == 0 || limit > 1024U * 1024U) return ESP_ERR_INVALID_ARG;
    *json = NULL;
    response_t response = {.limit = limit};
    esp_err_t err = perform(url, &response);
    if (err == ESP_OK) *json = response.data;
    else free(response.data);
    return err;
}

esp_err_t pdkpass_http_array(const char *url, pdkpass_http_item_fn item, void *context)
{
    if (!url || !item) return ESP_ERR_INVALID_ARG;
    char *buffer = malloc(4096);
    if (!buffer) {
        report_failure("stream-buffer", 0, ESP_ERR_NO_MEM, 0);
        return ESP_ERR_NO_MEM;
    }
    pdkpass_json_stream_t stream;
    response_t response = {.limit = 1024U * 1024U, .stream = &stream,
                           .item = item, .context = context};
    pdkpass_json_stream_init(&stream, buffer, 4096, stream_item, &response);
    esp_err_t err = perform(url, &response);
    free(buffer);
    return err;
}
