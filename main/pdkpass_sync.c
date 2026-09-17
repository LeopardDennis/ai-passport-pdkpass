#include "pdkpass_sync_policy.h"
#include "pdkpass_network.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_guard = portMUX_INITIALIZER_UNLOCKED;
static pdkpass_sync_policy_t s_policy;

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
