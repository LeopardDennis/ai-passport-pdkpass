#pragma once
#include <stdint.h>

// Called only by the UI I/O worker, outside the LVGL lock. No extra task.
// Normal builds are no-ops; diagnostic builds sample even while the LCD sleeps.
void pdkpass_battery_diagnostics_poll(void);
uint32_t pdkpass_battery_diagnostics_wait_ms(void);
