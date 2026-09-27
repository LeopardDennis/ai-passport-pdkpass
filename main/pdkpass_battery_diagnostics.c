#include "pdkpass_battery_diagnostics.h"
#include "sdkconfig.h"

#if CONFIG_PDKPASS_BATTERY_DIAGNOSTICS
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdbool.h>

#define DIAGNOSTIC_INTERVAL_US 60000000LL
static int64_t s_next_sample_us;

void pdkpass_battery_diagnostics_poll(void)
{
    int64_t now = esp_timer_get_time();
    if (now < s_next_sample_us) return;
    bsp_battery_diagnostics_t sample;
    esp_err_t err = bsp_battery_read_diagnostics(&sample);
    // Keep failures and rapid key/reminder wakeups at the same low log rate.
    s_next_sample_us = esp_timer_get_time() + DIAGNOSTIC_INTERVAL_US;
    int soc_x100 = sample.raw_soc < 0 ? -1 : sample.raw_soc * 100 / 256;
    bool soc_valid = sample.raw_soc >= 0 && (sample.raw_soc >> 8) <= 100;
    const char *mode = "UNKNOWN";
    if (sample.config >= 0) {
        switch (sample.config & 0xF0) {
        case 0x00: mode = "ACTIVE"; break;
        case 0x30: mode = "RESTART"; break;
        case 0xF0: mode = "SLEEP"; break;
        default: mode = "OTHER"; break;
        }
    }
    ESP_LOGI("battery_diag",
             "sample uptime_ms=%lld soc_raw=%d soc_x100=%d soc_valid=%d "
             "cell_raw=%d cell_mv=%d config=%d mode=%s version=%d read_error=%d",
             (long long)(now / 1000), sample.raw_soc, soc_x100, soc_valid,
             sample.raw_vcell, sample.cell_mv, sample.config, mode, sample.version,
             (int)err);
}

uint32_t pdkpass_battery_diagnostics_wait_ms(void)
{
    int64_t remaining = s_next_sample_us - esp_timer_get_time();
    if (remaining <= 0) return 0;
    return (uint32_t)((remaining + 999) / 1000);
}
#else
void pdkpass_battery_diagnostics_poll(void) {}
uint32_t pdkpass_battery_diagnostics_wait_ms(void) { return UINT32_MAX; }
#endif
