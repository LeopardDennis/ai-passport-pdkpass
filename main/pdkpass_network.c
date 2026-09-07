#include "pdkpass_network.h"

#include "pdkpass_wifi_form.h"
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
#define WIFI_RETRY_LIMIT 5
#define FORM_BODY_LIMIT 320
#define VALID_TIME_MIN 1767225600LL
#define VALID_TIME_MAX 4102444800LL
#define SNTP_SYNC_INTERVAL_MS (6U * 60U * 60U * 1000U)

#define EVENT_CONNECTED BIT0
#define EVENT_DISCONNECTED BIT1
#define EVENT_CANDIDATE BIT2
#define EVENT_TIME_SYNCED BIT3

static const char *TAG = "pdkpass_net";
static const char *NVS_NAMESPACE = "pdkpass_net";
static const char *SETUP_PAGE =
    "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
    "<title>PDKPASS Wi-Fi</title><style>body{font:18px system-ui;max-width:28rem;"
    "margin:3rem auto;padding:0 1rem}input,button{box-sizing:border-box;width:100%;"
    "font:inherit;padding:.8rem;margin:.35rem 0}button{font-weight:700}</style></head>"
    "<body><h1>PDKPASS Wi-Fi</h1><p>Connect this pass to a 2.4 GHz network for "
    "automatic Beijing time.</p><form method=post action=/save>"
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
static int64_t s_candidate_deadline;
static int64_t s_sync_deadline;
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

static void publish_state(pdkpass_network_state_t state)
{
    if (!s_callback) return;
    pdkpass_network_update_t update = {
        .state = state,
        .time_valid = current_time_valid(),
        .setup_ssid = s_in_setup ? s_setup_ssid : "",
        .setup_password = s_in_setup ? s_setup_password : "",
    };
    s_callback(&update);
}

static bool load_credentials(void)
{
    nvs_handle_t handle;
    size_t ssid_size = sizeof(s_working_ssid);
    size_t password_size = sizeof(s_working_password);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(handle, "ssid", s_working_ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "password", s_working_password,
                          &password_size);
    }
    nvs_close(handle);
    size_t password_length = strlen(s_working_password);
    return err == ESP_OK && ssid_size >= 2 && ssid_size <= sizeof(s_working_ssid) &&
           (password_length == 0 ||
            (password_length >= 8 && password_length <= 63));
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
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
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_events, EVENT_DISCONNECTED);
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

static esp_err_t start_setup(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(s_setup_ssid, sizeof(s_setup_ssid), "PDKPASS-%02X%02X",
             mac[4], mac[5]);
    // Wi-Fi is running here, so the hardware RNG has an active entropy source.
    uint8_t random_bytes[12];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        s_setup_password[i] = alphabet[random_bytes[i] & 31U];
    }
    s_setup_password[sizeof(random_bytes)] = '\0';

    wifi_config_t config = { 0 };
    memcpy(config.ap.ssid, s_setup_ssid, strlen(s_setup_ssid));
    config.ap.ssid_len = strlen(s_setup_ssid);
    memcpy(config.ap.password, s_setup_password, strlen(s_setup_password));
    config.ap.channel = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK) err = start_http_server();
    if (err != ESP_OK) return err;
    s_in_setup = true;
    s_testing_candidate = false;
    ESP_LOGI(TAG, "Wi-Fi setup ready; heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    publish_state(PDKPASS_NETWORK_SETUP);
    return ESP_OK;
}

static esp_err_t disconnect_station(void)
{
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) {
        EventBits_t stopped = xEventGroupWaitBits(s_events, EVENT_DISCONNECTED,
                                                 pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
        if (!(stopped & EVENT_DISCONNECTED)) return ESP_ERR_TIMEOUT;
    } else if (err != ESP_ERR_WIFI_NOT_CONNECT) return err;
    s_has_ip = false;
    xEventGroupClearBits(s_events, EVENT_CONNECTED | EVENT_DISCONNECTED);
    return ESP_OK;
}

static esp_err_t test_candidate(void)
{
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

    s_have_working_credentials = load_credentials();
    err = esp_wifi_set_mode(s_have_working_credentials ? WIFI_MODE_STA
                                                        : WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    if (s_have_working_credentials) {
        err = configure_station(s_working_ssid, s_working_password);
        if (err != ESP_OK) return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    if (s_have_working_credentials) {
        publish_state(PDKPASS_NETWORK_CONNECTING);
        return esp_wifi_connect();
    }
    return start_setup();
}

static void network_task(void *arg)
{
    (void)arg;
    unsigned retries = 0;
    esp_err_t err = prepare_network();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network startup failed: %s", esp_err_to_name(err));
        publish_state(PDKPASS_NETWORK_OFFLINE);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            s_events, EVENT_CONNECTED | EVENT_DISCONNECTED | EVENT_CANDIDATE |
                          EVENT_TIME_SYNCED,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));

        if (bits & EVENT_CANDIDATE) {
            err = test_candidate();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Candidate start failed: %s", esp_err_to_name(err));
                finish_candidate();
                publish_state(PDKPASS_NETWORK_SETUP);
            }
            // These bits were sampled before the new attempt was started.
            bits &= ~(EVENT_CONNECTED | EVENT_DISCONNECTED);
        }
        if (bits & EVENT_DISCONNECTED) {
            s_has_ip = false;
            s_sync_deadline = 0;
            if (s_testing_candidate) {
                finish_candidate();
                publish_state(PDKPASS_NETWORK_SETUP);
            } else if (!s_in_setup && s_have_working_credentials) {
                retries++;
                if (retries >= WIFI_RETRY_LIMIT) {
                    err = start_setup();
                    if (err != ESP_OK) publish_state(PDKPASS_NETWORK_OFFLINE);
                } else {
                    publish_state(PDKPASS_NETWORK_CONNECTING);
                    if (esp_wifi_connect() != ESP_OK) publish_state(PDKPASS_NETWORK_OFFLINE);
                }
            }
            bits &= ~EVENT_CONNECTED;
        }
        if (bits & EVENT_CONNECTED) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) continue;
            s_has_ip = true;
            retries = 0;
            if (s_testing_candidate) {
                err = accept_candidate();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Credentials not saved: %s", esp_err_to_name(err));
                    disconnect_station();
                    finish_candidate();
                    s_has_ip = false;
                    publish_state(PDKPASS_NETWORK_SETUP);
                    continue;
                }
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
        if (s_testing_candidate && now_us >= s_candidate_deadline) {
            disconnect_station();
            finish_candidate();
            s_has_ip = false;
            publish_state(PDKPASS_NETWORK_SETUP);
        }
        if (s_has_ip && s_sync_deadline && now_us >= s_sync_deadline) {
            if (!s_time_synced_boot) publish_state(PDKPASS_NETWORK_TIME_ERROR);
            start_sntp_once();
            s_sync_deadline = now_us + 300000000LL;
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
