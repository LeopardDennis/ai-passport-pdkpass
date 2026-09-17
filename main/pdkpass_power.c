#include "pdkpass_power.h"
#include "esp_pm.h"
#include <stddef.h>
static esp_pm_lock_handle_t s_display_cpu, s_display_awake, s_network_awake;
static bool s_display_active, s_network_active;

esp_err_t pdkpass_power_init(void)
{
    esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pdk_display", &s_display_cpu);
    if (err == ESP_OK) err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "pdk_display", &s_display_awake);
    if (err == ESP_OK) err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "pdk_network", &s_network_awake);
    if (err == ESP_OK) {
        pdkpass_power_display(true);
        esp_pm_config_t config = {.max_freq_mhz = 160, .min_freq_mhz = 80,
                                  .light_sleep_enable = true};
        err = esp_pm_configure(&config);
    }
    if (err != ESP_OK) {
        pdkpass_power_display(false);
        if (s_display_cpu) esp_pm_lock_delete(s_display_cpu);
        if (s_display_awake) esp_pm_lock_delete(s_display_awake);
        if (s_network_awake) esp_pm_lock_delete(s_network_awake);
        s_display_cpu = s_display_awake = s_network_awake = NULL;
    }
    return err;
}

void pdkpass_power_display(bool active)
{
    if (!s_display_cpu || !s_display_awake || active == s_display_active) return;
    if (active) {
        esp_pm_lock_acquire(s_display_awake);
        esp_pm_lock_acquire(s_display_cpu);
    } else {
        esp_pm_lock_release(s_display_cpu);
        esp_pm_lock_release(s_display_awake);
    }
    s_display_active = active;
}

void pdkpass_power_network(bool active)
{
    if (!s_network_awake || active == s_network_active) return;
    if (active) esp_pm_lock_acquire(s_network_awake);
    else esp_pm_lock_release(s_network_awake);
    s_network_active = active;
}
