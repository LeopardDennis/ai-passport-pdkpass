#include "pdkpass_network.h"
#include "pdkpass_http.h"
#include "pdkpass_sync_policy.h"
#include "pdkpass_power.h"

#include "pdkpass_wifi_form.h"
#include "pdkpass_wifi_profiles.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define NETWORK_TASK_STACK 4096
#define NETWORK_TASK_PRIORITY 4
#define SAVED_CONNECT_TIMEOUT_US 15000000LL
#define FORM_BODY_LIMIT 320
#define VALID_TIME_MIN 1767225600LL
#define VALID_TIME_MAX 4102444800LL
#define SNTP_SYNC_INTERVAL_MS (6U * 60U * 60U * 1000U)

#define EVENT_CONNECTED BIT0
#define EVENT_DISCONNECTED BIT1
#define EVENT_CANDIDATE BIT2
#define EVENT_TIME_SYNCED BIT3
#define EVENT_STOPPED BIT4
#define EVENT_ASSOCIATED BIT5
#define EVENT_AUTH_ERROR BIT6
#define EVENT_AP_MISSING BIT7
#define EVENT_SECURITY_ERROR BIT8
#define EVENT_RETRY BIT9
#define EVENT_SETUP BIT10
#define EVENT_CANCEL BIT11
#define EVENT_CLIENT BIT12
#define EVENT_SYNC BIT13
#define EVENT_POLICY BIT14
#define STATION_EVENTS (EVENT_CONNECTED | EVENT_DISCONNECTED | EVENT_ASSOCIATED | \
                        EVENT_AUTH_ERROR | EVENT_AP_MISSING | EVENT_SECURITY_ERROR)

static const char *TAG = "pdkpass_net";
static const char *NVS_NAMESPACE = "pdkpass_net";
static const char *SETUP_PAGE =
    "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
    "<title>PDKPASS Wi-Fi</title><style>body{font:18px system-ui;max-width:28rem;"
    "margin:3rem auto;padding:0 1rem}input,button{box-sizing:border-box;width:100%;"
    "font:inherit;padding:.8rem;margin:.35rem 0}button{font-weight:700}</style></head>"
    "<body><h1>PDKPASS Wi-Fi</h1><p>Connect this pass to a 2.4 GHz network for "
    "automatic Beijing time. Remembers up to 5 networks; a sixth replaces "
    "the least recently connected network.</p><form method=post action=/save>"
    "<label>Wi-Fi name<input name=ssid maxlength=32 required></label>"
    "<label>Password<input name=password type=password maxlength=63></label>"
    "<button type=submit>Connect</button></form></body></html>";

static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_candidate_lock;
static pdkpass_network_callback_t s_callback;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_http;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static char s_working_ssid[33];
static char s_working_password[65];
// Owned exclusively by the network worker; HTTP only queues candidates.
static pdkpass_wifi_profiles_t s_profiles = {.version = 1};
static unsigned s_saved_attempt;
static int64_t s_saved_deadline;
static char s_candidate_ssid[33];
static char s_candidate_password[65];
static char s_setup_ssid[33];
static char s_setup_password[16];
static bool s_have_working_credentials;
static bool s_testing_candidate;
static bool s_in_setup;
static bool s_sntp_started;
static bool s_candidate_busy; // protected by s_candidate_lock, includes queued attempts
static bool s_time_synced_boot;
static bool s_has_ip;
// Worker-owned: true from a successful connect call until disconnect/stop.
static bool s_station_active;
static bool s_associated;
static bool s_radio_started;
static bool s_visible[PDKPASS_WIFI_PROFILE_LIMIT];
static int64_t s_setup_started;
static int64_t s_setup_idle_since;
static pdkpass_network_state_t s_published_state = PDKPASS_NETWORK_STARTING;
static const char *s_setup_error = "";
static int64_t s_candidate_deadline;
static int64_t s_sync_deadline;
static int64_t s_idle_check;
static bool s_auto_parked;
static char s_attempt_ssid[33];
static char s_attempt_password[65];

static void finish_candidate(void)
{
    xSemaphoreTake(s_candidate_lock, portMAX_DELAY);
    s_candidate_busy = false;
    memset(s_candidate_password, 0, sizeof(s_candidate_password));
    xSemaphoreGive(s_candidate_lock);
    s_testing_candidate = false;
    s_candidate_deadline = 0;
    memset(s_attempt_password, 0, sizeof(s_attempt_password));
}


static bool current_time_valid(void)
{
    int64_t now = (int64_t)time(NULL);
    return now >= VALID_TIME_MIN && now <= VALID_TIME_MAX;
}

static int64_t setup_deadline(void)
{
    int64_t deadline = s_setup_started + 600000000LL;
    if (s_setup_idle_since >= 0 && !s_testing_candidate &&
        s_setup_idle_since + 180000000LL < deadline)
        deadline = s_setup_idle_since + 180000000LL;
    return deadline;
}

static void publish_state(pdkpass_network_state_t state)
{
    pdkpass_power_network(s_in_setup || state == PDKPASS_NETWORK_STARTING ||
                          state == PDKPASS_NETWORK_CONNECTING || state == PDKPASS_NETWORK_SYNCING);
    s_published_state = state;
    if (!s_callback) return;
    int64_t remaining = s_in_setup ? setup_deadline() - esp_timer_get_time() : 0;
    pdkpass_network_update_t update = {
        .state = state,
        .time_valid = current_time_valid(),
        .setup_ssid = s_in_setup ? s_setup_ssid : "",
        .setup_password = s_in_setup ? s_setup_password : "",
        .setup_error = s_setup_error,
        .hotspot_active = s_in_setup,
        .setup_seconds_left = remaining > 0 ? (unsigned)((remaining + 999999) / 1000000) : 0,
    };
    s_callback(&update);
}

static bool load_credentials(void)
{
    nvs_handle_t handle;
    size_t ssid_size = sizeof(s_working_ssid);
    size_t password_size = sizeof(s_working_password);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t blob_size = sizeof(s_profiles);
    esp_err_t blob_err = nvs_get_blob(handle, "profiles", &s_profiles, &blob_size);
    if (blob_err == ESP_OK && blob_size == sizeof(s_profiles) &&
        pdkpass_wifi_profiles_valid(&s_profiles) && s_profiles.count) {
        nvs_close(handle);
        memcpy(s_working_ssid, s_profiles.entries[0].ssid, sizeof(s_working_ssid));
        memcpy(s_working_password, s_profiles.entries[0].password, sizeof(s_working_password));
        return true;
    }
    // Old firmware stored one SSID/password pair. Import without erasing it;
    // the first successful connection commits the versioned multi-network blob.
    memset(&s_profiles, 0, sizeof(s_profiles));
    s_profiles.version = 1;
    esp_err_t err = nvs_get_str(handle, "ssid", s_working_ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "password", s_working_password,
                          &password_size);
    }
    nvs_close(handle);
    return err == ESP_OK && pdkpass_wifi_profiles_remember(
        &s_profiles, s_working_ssid, s_working_password);
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    pdkpass_wifi_profiles_t next = s_profiles;
    if (!pdkpass_wifi_profiles_remember(&next, ssid, password)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    // One blob avoids partially saved SSID/password pairs. Only publish the new
    // in-memory list after NVS commits successfully, preserving old credentials.
    err = nvs_set_blob(handle, "profiles", &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_profiles = next;
    return err;
}

static void restore_last_time(void)
{
    nvs_handle_t handle;
    int64_t stored_time = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    esp_err_t err = nvs_get_i64(handle, "last_time", &stored_time);
    nvs_close(handle);
    if (err != ESP_OK || stored_time < VALID_TIME_MIN ||
        stored_time > VALID_TIME_MAX) return;

    struct timeval tv = { .tv_sec = (time_t)stored_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void save_current_time(void)
{
    int64_t now = (int64_t)time(NULL);
    if (now < VALID_TIME_MIN || now > VALID_TIME_MAX) return;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_i64(handle, "last_time", now) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        EventBits_t bits = EVENT_DISCONNECTED;
        const wifi_event_sta_disconnected_t *event = data;
        if (event) {
            ESP_LOGW(TAG, "Station disconnected: reason=%u", event->reason);
            switch (event->reason) {
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT: bits |= EVENT_AUTH_ERROR; break;
            case WIFI_REASON_NO_AP_FOUND:
            case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: bits |= EVENT_AP_MISSING; break;
            case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: bits |= EVENT_SECURITY_ERROR; break;
            default: break;
            }
        }
        xEventGroupSetBits(s_events, bits);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        xEventGroupSetBits(s_events, EVENT_STOPPED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(s_events, EVENT_ASSOCIATED);
    } else if (base == WIFI_EVENT &&
               (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        xEventGroupSetBits(s_events, EVENT_CLIENT);
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, EVENT_CONNECTED);
    }
}

static void time_sync_notification(struct timeval *tv)
{
    (void)tv;
    xEventGroupSetBits(s_events, EVENT_TIME_SYNCED);
}

static void start_sntp_once(void)
{
    if (s_sntp_started) {
        esp_sntp_restart();
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    esp_sntp_setservername(1, "time.cloudflare.com");
#endif
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 2
    esp_sntp_setservername(2, "time.google.com");
#endif
    esp_sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();
    s_sntp_started = true;
}

static esp_err_t root_get(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(request, SETUP_PAGE);
}

static esp_err_t save_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > FORM_BODY_LIMIT) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_FAIL;
    }

    char body[FORM_BODY_LIMIT + 1];
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(request, body + received,
                                    request->content_len - received);
        if (result <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                "Incomplete form");
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    body[received] = '\0';

    char ssid[33];
    char password[65];
    if (!pdkpass_wifi_form_parse(body, received, ssid, sizeof(ssid),
                                  password, sizeof(password))) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                            "Check Wi-Fi name and password");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_candidate_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Please try again");
        return ESP_FAIL;
    }
    if (s_candidate_busy) {
        xSemaphoreGive(s_candidate_lock);
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "A connection is already being tested. Please wait.");
    }
    s_candidate_busy = true;
    memcpy(s_candidate_ssid, ssid, sizeof(s_candidate_ssid));
    memcpy(s_candidate_password, password, sizeof(s_candidate_password));
    xSemaphoreGive(s_candidate_lock);
    xEventGroupSetBits(s_events, EVENT_CANDIDATE);

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(request,
        "<!doctype html><meta name=viewport content='width=device-width'>"
        "<h1>Testing Wi-Fi...</h1><p>Check PDKPASS for the result. If setup "
        "remains visible, reconnect and check the password.</p>");
}

static esp_err_t start_http_server(void)
{
    if (s_http) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 6144;

    esp_err_t err = httpd_start(&s_http, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post,
    };
    err = httpd_register_uri_handler(s_http, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_http, &save);
    if (err != ESP_OK) {
        httpd_stop(s_http);
        s_http = NULL;
    }
    return err;
}

static void stop_http_server(void)
{
    if (!s_http) return;
    httpd_stop(s_http);
    s_http = NULL;
}

static esp_err_t configure_station(const char *ssid, const char *password)
{
    wifi_config_t config = { 0 };
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK
                                                : WIFI_AUTH_OPEN;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static void generate_setup_password(void)
{
    // Called with Wi-Fi running so the RNG has an active entropy source.
    uint8_t random_bytes[8];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        s_setup_password[i] = alphabet[random_bytes[i] & 31U];
    }
    s_setup_password[sizeof(random_bytes)] = '\0';
}

static esp_err_t start_setup(void)
{
    s_saved_deadline = 0;
    if (s_in_setup) {
        publish_state(PDKPASS_NETWORK_SETUP);
        return ESP_OK;
    }
    // Bring up STA only first: enable RNG entropy without exposing an old AP.
    if (!s_radio_started) {
        esp_err_t start_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (start_err == ESP_OK) start_err = esp_wifi_start();
        if (start_err != ESP_OK) return start_err;
        s_radio_started = true;
    }
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(s_setup_ssid, sizeof(s_setup_ssid), "PDKPASS-%02X%02X",
             mac[4], mac[5]);
    generate_setup_password();

    wifi_config_t config = { 0 };
    memcpy(config.ap.ssid, s_setup_ssid, strlen(s_setup_ssid));
    config.ap.ssid_len = strlen(s_setup_ssid);
    memcpy(config.ap.password, s_setup_password, strlen(s_setup_password));
    config.ap.channel = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK && !s_radio_started) {
        err = esp_wifi_start();
        s_radio_started = err == ESP_OK;
    }
    if (err == ESP_OK) err = start_http_server();
    if (err != ESP_OK) return err;
    s_in_setup = true;
    s_setup_started = esp_timer_get_time();
    s_setup_idle_since = s_setup_started;
    s_testing_candidate = false;
    ESP_LOGI(TAG, "Wi-Fi setup ready; heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    publish_state(PDKPASS_NETWORK_SETUP);
    return ESP_OK;
}

static esp_err_t disconnect_station(void)
{
    // An idle station has no disconnect event to wait for. When cancelling an
    // active scan/association/link, STA_STOP is the event-loop barrier: all old
    // connection events have drained before the next attempt starts. AP config
    // and the HTTP server are retained; phones may need to rejoin the setup AP.
    if (s_station_active) {
        xEventGroupClearBits(s_events, EVENT_STOPPED);
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) return err;
        EventBits_t stopped = xEventGroupWaitBits(s_events, EVENT_STOPPED,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
        if (!(stopped & EVENT_STOPPED)) return ESP_ERR_TIMEOUT;
        xEventGroupClearBits(s_events, STATION_EVENTS);
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
    }
    s_station_active = false;
    s_associated = false;
    s_has_ip = false;
    s_sync_deadline = 0;
    xEventGroupClearBits(s_events, STATION_EVENTS);
    return ESP_OK;
}

static esp_err_t test_candidate(void)
{
    s_setup_error = "";
    publish_state(PDKPASS_NETWORK_CONNECTING);
    s_saved_deadline = 0;
    xSemaphoreTake(s_candidate_lock, portMAX_DELAY);
    memcpy(s_attempt_ssid, s_candidate_ssid, sizeof(s_attempt_ssid));
    memcpy(s_attempt_password, s_candidate_password, sizeof(s_attempt_password));
    xSemaphoreGive(s_candidate_lock);

    // Drain the old connection before applying a new configuration. The form
    // remains busy until this immutable attempt succeeds, fails or times out.
    esp_err_t err = disconnect_station();
    if (err != ESP_OK) return err;
    err = configure_station(s_attempt_ssid, s_attempt_password);
    if (err == ESP_OK) {
        s_testing_candidate = true;
        s_candidate_deadline = esp_timer_get_time() + 30000000LL;
        publish_state(PDKPASS_NETWORK_CONNECTING);
        err = esp_wifi_connect();
        s_station_active = err == ESP_OK;
    }
    return err;
}

static esp_err_t accept_candidate(void)
{
    if (xSemaphoreTake(s_candidate_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK && strncmp((const char *)ap.ssid, s_attempt_ssid, 32) != 0) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) err = save_credentials(s_attempt_ssid, s_attempt_password);
    if (err == ESP_OK) {
        memcpy(s_working_ssid, s_attempt_ssid, sizeof(s_working_ssid));
        memcpy(s_working_password, s_attempt_password,
               sizeof(s_working_password));
        s_have_working_credentials = true;
    }
    xSemaphoreGive(s_candidate_lock);
    if (err != ESP_OK) return err;

    stop_http_server();
    s_in_setup = false;
    finish_candidate();
    return esp_wifi_set_mode(WIFI_MODE_STA);
}

static esp_err_t go_offline(void)
{
    if (s_radio_started) {
        xEventGroupClearBits(s_events, EVENT_STOPPED);
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Radio stop failed: %s", esp_err_to_name(err));
            return err;
        }
        xEventGroupWaitBits(s_events, EVENT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
        s_radio_started = false;
    }
    // Cut radio power before waiting for any outstanding HTTP handler to exit.
    stop_http_server();
    s_in_setup = false;
    s_station_active = false;
    s_associated = false;
    s_has_ip = false;
    s_saved_deadline = 0;
    s_sync_deadline = 0;
    finish_candidate();
    xEventGroupClearBits(s_events, STATION_EVENTS | EVENT_CANDIDATE | EVENT_CLIENT);
    publish_state(PDKPASS_NETWORK_OFFLINE);
    return ESP_OK;
}

// Start one bounded saved-network attempt. Run only from the worker, after
// draining the previous station. Setup stays available during background retry.
static esp_err_t connect_saved(void)
{
    size_t index = pdkpass_wifi_profile_for_attempt(s_profiles.count, s_saved_attempt);
    while (index < s_profiles.count && !s_visible[index]) {
        s_saved_attempt = (unsigned)(index + 1U) * 2U;
        index = pdkpass_wifi_profile_for_attempt(s_profiles.count, s_saved_attempt);
    }
    if (index >= s_profiles.count) {
        s_setup_error = "NO AVAILABLE NETWORK";
        return go_offline();
    }
    memcpy(s_working_ssid, s_profiles.entries[index].ssid, sizeof(s_working_ssid));
    memcpy(s_working_password, s_profiles.entries[index].password, sizeof(s_working_password));
    esp_err_t err = configure_station(s_working_ssid, s_working_password);
    s_saved_deadline = esp_timer_get_time() + SAVED_CONNECT_TIMEOUT_US;
    if (!s_in_setup) publish_state(PDKPASS_NETWORK_CONNECTING);
    if (err == ESP_OK) err = esp_wifi_connect();
    s_station_active = err == ESP_OK;
    // Immediate driver errors use the same bounded fallback path.
    if (err != ESP_OK) s_saved_deadline = esp_timer_get_time();
    return ESP_OK;
}

static esp_err_t scan_saved(void)
{
    if (!s_profiles.count) {
        s_setup_error = "NO SAVED NETWORKS";
        return go_offline();
    }
    s_setup_error = "";
    publish_state(PDKPASS_NETWORK_CONNECTING);
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK && !s_radio_started) {
        err = esp_wifi_start();
        s_radio_started = err == ESP_OK;
    }
    memset(s_visible, 0, sizeof(s_visible));
    // Bounded single scan on the worker, never the UI/button task. Consume
    // records individually instead of allocating an array of nearby APs.
    wifi_scan_config_t config = {.show_hidden = true};
    config.scan_time.active.min = 30;
    config.scan_time.active.max = 120;
    if (err == ESP_OK) err = esp_wifi_scan_start(&config, true);
    uint16_t count = 0;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_num(&count);
    for (uint16_t i = 0; err == ESP_OK && i < count; i++) {
        wifi_ap_record_t ap;
        err = esp_wifi_scan_get_ap_record(&ap);
        if (err != ESP_OK) break;
        for (size_t j = 0; j < s_profiles.count; j++) {
            if (strncmp((const char *)ap.ssid, s_profiles.entries[j].ssid, 32) == 0)
                s_visible[j] = true;
        }
    }
    esp_wifi_clear_ap_list();
    if (err != ESP_OK) {
        s_setup_error = "SCAN FAILED / RETRY";
        go_offline();
        return err;
    }
    // A cancel/setup request received while scanning takes precedence.
    if (xEventGroupGetBits(s_events) & (EVENT_CANCEL | EVENT_SETUP)) return ESP_OK;
    s_saved_attempt = 0;
    return connect_saved();
}

static esp_err_t configure_setup_address(void)
{
    // Configure before Wi-Fi starts. Restarting DHCP on a down interface arms
    // it for AP_START, using the new subnet for phone leases as well.
    esp_netif_ip_info_t info = {0};
    esp_err_t err = esp_netif_str_to_ip4(PDKPASS_SETUP_IP, &info.ip);
    if (err != ESP_OK) return err;
    info.gw = info.ip;
    err = esp_netif_str_to_ip4("255.255.255.0", &info.netmask);
    if (err != ESP_OK) return err;
    err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_ap_netif, &info);
    if (err != ESP_OK) return err;
    return esp_netif_dhcps_start(s_ap_netif);
}

static esp_err_t prepare_network(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed without erase: %s", esp_err_to_name(err));
        return err;
    }
    restore_last_time();
    setenv("TZ", "CST-8", 1);
    tzset();
    publish_state(PDKPASS_NETWORK_STARTING);

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) return ESP_ERR_NO_MEM;
    err = configure_setup_address();
    if (err != ESP_OK) return err;

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event, NULL,
                                              &s_wifi_handler);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) return err;

    s_have_working_credentials = load_credentials();
    // No saved profiles means no scan, radio or hotspot on first boot.
    scan_saved();
    return ESP_OK;
}

// Sleep until the next active deadline; Wi-Fi, form and SNTP events wake us
// immediately. An idle, synchronized connection needs no periodic polling.
static TickType_t network_wait_ticks(int64_t now_us)
{
    const int64_t deadlines[] = {
        s_saved_deadline,
        s_testing_candidate ? s_candidate_deadline : 0,
        s_has_ip ? s_sync_deadline : 0,
        s_in_setup ? now_us + 1000000LL : 0,
        s_in_setup ? setup_deadline() : 0,
        s_has_ip && !s_in_setup ? s_idle_check : 0,
    };
    int64_t next = 0;
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); ++i) {
        if (deadlines[i] && (!next || deadlines[i] < next)) next = deadlines[i];
    }
    if (!next) return portMAX_DELAY;
    if (next <= now_us) return 1;
    // Round up to milliseconds and retain at least one RTOS tick.
    TickType_t ticks = pdMS_TO_TICKS((next - now_us + 999) / 1000);
    return ticks ? ticks : 1;
}

static void network_task(void *arg)
{
    (void)arg;
    esp_err_t err = prepare_network();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network startup failed: %s", esp_err_to_name(err));
        publish_state(PDKPASS_NETWORK_OFFLINE);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            s_events, STATION_EVENTS | EVENT_CANDIDATE |
                          EVENT_TIME_SYNCED | EVENT_RETRY | EVENT_SETUP | EVENT_CANCEL | EVENT_CLIENT |
                          EVENT_SYNC | EVENT_POLICY,
            pdTRUE, pdFALSE, network_wait_ticks(esp_timer_get_time()));

        if (bits & EVENT_CANCEL) {
            s_auto_parked = false;
            if (s_in_setup || !s_has_ip) go_offline();
            continue;
        }
        if (bits & EVENT_SETUP) {
            s_auto_parked = false;
            if (!s_in_setup) {
                go_offline();
                s_setup_error = "";
                if (start_setup() != ESP_OK) {
                    s_setup_error = "START FAILED / RETRY";
                    go_offline();
                }
            }
            continue;
        }
        if (bits & EVENT_RETRY) {
            s_auto_parked = false;
            if (!s_has_ip && !s_in_setup && !s_saved_deadline) scan_saved();
            else publish_state(s_published_state);
            continue;
        }
        if ((bits & EVENT_SYNC) && s_auto_parked && !s_in_setup && !s_has_ip) {
            s_auto_parked = false;
            scan_saved();
            continue;
        }
        if (s_in_setup) {
            wifi_sta_list_t clients;
            if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
                if (clients.num) s_setup_idle_since = -1;
                else if (s_setup_idle_since < 0) s_setup_idle_since = esp_timer_get_time();
            }
            if (esp_timer_get_time() >= setup_deadline()) {
                s_setup_error = "SETUP EXPIRED";
                go_offline();
                continue;
            }
            publish_state(s_published_state);
        }
        if ((bits & EVENT_CANDIDATE) && s_in_setup) {
            err = test_candidate();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Candidate start failed: %s", esp_err_to_name(err));
                s_setup_error = "START FAILED / RETRY";
                finish_candidate();
                publish_state(PDKPASS_NETWORK_SETUP);
            }
            // These bits were sampled before the new attempt was started.
            bits &= ~STATION_EVENTS;
        }
        if (bits & EVENT_ASSOCIATED) s_associated = true;
        if (bits & EVENT_DISCONNECTED) {
            s_station_active = false;
            s_associated = false;
            s_has_ip = false;
            s_sync_deadline = 0;
            if (s_testing_candidate) {
                s_setup_error = bits & EVENT_AUTH_ERROR ? "AUTH FAILED / RETRY" :
                    bits & EVENT_AP_MISSING ? "WIFI NOT FOUND" :
                    bits & EVENT_SECURITY_ERROR ? "WIFI SECURITY ERROR" : "CONNECTION FAILED";
                finish_candidate();
                publish_state(PDKPASS_NETWORK_SETUP);
            } else if (s_have_working_credentials && (!s_in_setup || s_saved_deadline)) {
                // A dropped working link restarts at the most recent network;
                // a failed connection advances through the bounded attempt list.
                if (s_saved_deadline) {
                    ++s_saved_attempt;
                    err = connect_saved();
                } else {
                    err = scan_saved();
                }
                if (err != ESP_OK) publish_state(PDKPASS_NETWORK_OFFLINE);
            }
            bits &= ~EVENT_CONNECTED;
        }
        if (bits & EVENT_CONNECTED) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) continue;
            if (!s_testing_candidate && (!s_saved_deadline ||
                strncmp((const char *)ap.ssid, s_working_ssid, 32) != 0)) continue;
            s_has_ip = true;
            s_idle_check = esp_timer_get_time() + 30000000LL;
            s_saved_deadline = 0;
            s_saved_attempt = 0;
            if (s_testing_candidate) {
                err = accept_candidate();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Credentials not saved: %s", esp_err_to_name(err));
                    s_setup_error = "SAVE FAILED / RETRY";
                    disconnect_station();
                    finish_candidate();
                    s_has_ip = false;
                    publish_state(PDKPASS_NETWORK_SETUP);
                    continue;
                }
            } else {
                // Also persists the legacy import; NVS skips unchanged blobs.
                err = save_credentials(s_working_ssid, s_working_password);
                if (err != ESP_OK) ESP_LOGW(TAG, "Wi-Fi order not saved: %s", esp_err_to_name(err));
                stop_http_server();
                s_in_setup = false;
                esp_wifi_set_mode(WIFI_MODE_STA);
            }
            publish_state(s_time_synced_boot && current_time_valid()
                              ? PDKPASS_NETWORK_ONLINE : PDKPASS_NETWORK_SYNCING);
            start_sntp_once();
            s_sync_deadline = esp_timer_get_time() + 60000000LL;
        }
        if (bits & EVENT_TIME_SYNCED) {
            s_time_synced_boot = current_time_valid();
            if (s_time_synced_boot) save_current_time();
            s_sync_deadline = 0;
            if (s_has_ip && !s_in_setup && s_time_synced_boot) publish_state(PDKPASS_NETWORK_ONLINE);
        }
        int64_t now_us = esp_timer_get_time();
        if (s_saved_deadline && now_us >= s_saved_deadline) {
            err = disconnect_station();
            if (err == ESP_OK) {
                ++s_saved_attempt;
                err = connect_saved();
            } else {
                s_saved_deadline = 0;
                err = go_offline();
            }
            if (err != ESP_OK) publish_state(PDKPASS_NETWORK_OFFLINE);
        }
        if (s_testing_candidate && now_us >= s_candidate_deadline) {
            s_setup_error = s_associated ? "IP ADDRESS TIMEOUT" : "CONNECTION TIMEOUT";
            disconnect_station();
            finish_candidate();
            s_has_ip = false;
            publish_state(PDKPASS_NETWORK_SETUP);
        }
        if (s_has_ip && s_sync_deadline && now_us >= s_sync_deadline) {
            if (!s_time_synced_boot) {
                s_setup_error = "TIME SYNC FAILED";
                publish_state(PDKPASS_NETWORK_TIME_ERROR);
                go_offline();
                continue;
            }
            start_sntp_once();
            s_sync_deadline = now_us + 300000000LL;
        }
        if (s_has_ip && !s_in_setup && now_us >= s_idle_check) {
            s_idle_check = now_us + 30000000LL;
            // Serialize shutdown with complete service transactions. Services
            // recheck connectivity after acquiring this same mutex.
            if (s_time_synced_boot && pdkpass_sync_idle() && pdkpass_http_try_begin()) {
                if (pdkpass_sync_idle()) {
                    s_auto_parked = go_offline() == ESP_OK;
                }
                pdkpass_http_end();
            }
        }
    }
}

esp_err_t pdkpass_network_start(pdkpass_network_callback_t callback)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    if (s_events) return ESP_ERR_INVALID_STATE;
    s_callback = callback;
    s_events = xEventGroupCreate();
    s_candidate_lock = xSemaphoreCreateMutex();
    if (!s_events || !s_candidate_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(network_task, "pdkpass_net", NETWORK_TASK_STACK, NULL,
                    NETWORK_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_network_request(pdkpass_network_command_t command)
{
    if (!s_events) return;
    EventBits_t bits = command == PDKPASS_NETWORK_CANCEL ? EVENT_CANCEL :
        command == PDKPASS_NETWORK_SYNC ? EVENT_SYNC :
        command == PDKPASS_NETWORK_POLICY ? EVENT_POLICY :
        command == PDKPASS_NETWORK_OPEN_SETUP ? EVENT_SETUP : EVENT_RETRY;
    xEventGroupSetBits(s_events, bits);
}
