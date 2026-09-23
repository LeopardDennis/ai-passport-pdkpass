#include "pdkpass_sync_policy.h"
#include "pdkpass_network.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static portMUX_TYPE s_guard = portMUX_INITIALIZER_UNLOCKED;
static pdkpass_sync_policy_t s_policy;
static int64_t s_last_success[PDKPASS_SYNC_COUNT];
static void (*s_status_callback)(void);
static const char *const s_status_keys[PDKPASS_SYNC_COUNT] = {
    "calendar", "results",
};

void pdkpass_sync_status_init(void (*callback)(void))
{
    s_status_callback = callback;
    nvs_handle_t handle;
    if (nvs_open("pdk_sync", NVS_READONLY, &handle) != ESP_OK) return;
    for (unsigned i = 0; i < PDKPASS_SYNC_COUNT; i++) {
        int64_t saved = 0;
        if (nvs_get_i64(handle, s_status_keys[i], &saved) == ESP_OK &&
            saved >= 1767225600LL && saved <= 4102444800LL) {
            s_last_success[i] = saved;
        }
    }
    nvs_close(handle);
}

int64_t pdkpass_sync_last_success(pdkpass_sync_service_t service)
{
    if ((unsigned)service >= PDKPASS_SYNC_COUNT) return 0;
    portENTER_CRITICAL(&s_guard);
    int64_t last = s_last_success[service];
    portEXIT_CRITICAL(&s_guard);
    return last;
}

void pdkpass_sync_mark_success(pdkpass_sync_service_t service, int64_t utc)
{
    if ((unsigned)service >= PDKPASS_SYNC_COUNT ||
        utc < 1767225600LL || utc > 4102444800LL) return;
    int64_t previous = pdkpass_sync_last_success(service);
    if (previous > 0 && (previous + 28800LL) / 86400LL ==
                        (utc + 28800LL) / 86400LL) return;

    nvs_handle_t handle;
    if (nvs_open("pdk_sync", NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t err = nvs_set_i64(handle, s_status_keys[service], utc);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return;
    portENTER_CRITICAL(&s_guard);
    s_last_success[service] = utc;
    portEXIT_CRITICAL(&s_guard);
    if (s_status_callback) s_status_callback();
}

void pdkpass_sync_plan(pdkpass_sync_service_t service, uint32_t delay_ms)
{
    if ((unsigned)service >= PDKPASS_SYNC_COUNT) return;
    int64_t due = esp_timer_get_time() / 1000 + delay_ms;
    portENTER_CRITICAL(&s_guard);
    s_policy.due_ms[service] = due;
    portEXIT_CRITICAL(&s_guard);
    pdkpass_network_request(PDKPASS_NETWORK_POLICY);
}

uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t service)
{
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_guard);
    uint32_t wait = pdkpass_sync_policy_wait(&s_policy, service, now);
    portEXIT_CRITICAL(&s_guard);
    return wait;
}

bool pdkpass_sync_idle(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_guard);
    bool idle = pdkpass_sync_policy_idle(&s_policy, now);
    portEXIT_CRITICAL(&s_guard);
    return idle;
}
