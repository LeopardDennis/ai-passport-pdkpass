#include "pdkpass_http.h"
#include "pdkpass_json_stream.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static SemaphoreHandle_t s_transaction;
static int64_t s_retry_at_us;
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

typedef struct {
    size_t free_bytes;
    size_t largest_block;
} heap_sample_t;

static heap_sample_t sample_heap(void)
{
    return (heap_sample_t){
        .free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
    };
}

static void report_failure(const char *stage, int status, esp_err_t err,
                           size_t bytes, heap_sample_t before,
                           heap_sample_t active, heap_sample_t after)
{
    // The low-water mark is since boot, not necessarily caused by this request.
    ESP_LOGW(TAG, "GET stage=%s status=%d err=%s bytes=%u "
             "heap(free/largest)=%u/%u -> %u/%u -> %u/%u low=%u",
             stage, status, esp_err_to_name(err), (unsigned)bytes,
             (unsigned)before.free_bytes, (unsigned)before.largest_block,
             (unsigned)active.free_bytes, (unsigned)active.largest_block,
             (unsigned)after.free_bytes, (unsigned)after.largest_block,
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

void pdkpass_http_report_data_failure(const char *stage, esp_err_t err,
                                      size_t bytes)
{
    heap_sample_t heap = sample_heap();
    report_failure(stage, err == ESP_ERR_NO_MEM ? 0 : 200, err, bytes,
                   heap, heap, heap);
}

esp_err_t pdkpass_http_init(void)
{
    if (!s_transaction) s_transaction = xSemaphoreCreateMutex();
    return s_transaction ? ESP_OK : ESP_ERR_NO_MEM;
}
void pdkpass_http_begin(void) { xSemaphoreTake(s_transaction, portMAX_DELAY); }
bool pdkpass_http_try_begin(void) { return s_transaction && xSemaphoreTake(s_transaction, 0) == pdTRUE; }
void pdkpass_http_end(void) { xSemaphoreGive(s_transaction); }

static bool stream_item(const char *json, void *context)
{
    response_t *response = context;
    const char *end = NULL;
    cJSON *object = cJSON_ParseWithOpts(json, &end, true);
    bool ok = cJSON_IsObject(object) && response->item(object, response->context);
    cJSON_Delete(object);
    return ok;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (!r) return ESP_OK;
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
            r->failure_stage = "json-stream";
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

static esp_err_t perform(const char *url, response_t *r)
{
    if (esp_timer_get_time() < s_retry_at_us) return ESP_ERR_TIMEOUT;
    heap_sample_t before = sample_heap();
    esp_http_client_config_t config = {
        .url = url, .event_handler = http_event, .user_data = r,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000,
        .buffer_size = 1024, .user_agent = "PDKPASS/1.1",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        heap_sample_t after = sample_heap();
        report_failure("client-init", 0, ESP_ERR_NO_MEM, 0,
                       before, after, after);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t transport_err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_err_t err = r->error != ESP_OK ? r->error : transport_err;
    if (err == ESP_OK && (status != 200 || r->length == 0)) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && r->stream && !pdkpass_json_stream_done(r->stream))
        err = ESP_ERR_INVALID_RESPONSE;
    heap_sample_t active = {0};
    if (err != ESP_OK) active = sample_heap();
    esp_http_client_cleanup(client);
    if (status == 429 || status == 503) {
        unsigned seconds = r->retry_seconds ? r->retry_seconds : 60;
        s_retry_at_us = esp_timer_get_time() + (int64_t)seconds * 1000000LL;
    }
    if (err != ESP_OK) {
        const char *stage = r->failure_stage ? r->failure_stage
                            : transport_err != ESP_OK ? "transport"
                            : status != 200 ? "http-status"
                            : r->length == 0 ? "empty-body" : "stream-end";
        report_failure(stage, status, err, r->length,
                       before, active, sample_heap());
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
        heap_sample_t heap = sample_heap();
        report_failure("stream-buffer", 0, ESP_ERR_NO_MEM, 0,
                       heap, heap, heap);
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
