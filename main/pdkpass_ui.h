#pragma once

#include "bsp_button.h"
#include "pdkpass_network.h"
#include <stdbool.h>
#include <stddef.h>

// Create the long-lived PDKPASS screen and its LVGL timers. Call while holding
// the BSP LVGL lock after display/LVGL initialization.
void pdkpass_ui_enter(bool battery_available);

// Dispatch one hardware key event. The caller must hold the BSP LVGL lock.
void pdkpass_ui_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// Update connectivity and time state. The caller must hold the BSP LVGL lock.
void pdkpass_ui_network_update(const pdkpass_network_update_t *update);

// Refresh a visible session-results page after its background cache changes.
// The caller must hold the BSP LVGL lock.
void pdkpass_ui_results_update(size_t race_index);

// Reload the atomically published season snapshot and redraw the active page.
// The caller must hold the BSP LVGL lock.
void pdkpass_ui_season_update(void);

// Publish a worker-sampled SOC; caller holds the LVGL lock. No I2C in UI timers.
void pdkpass_ui_battery_update(int soc);

// Read the backlight-off state while holding the LVGL lock. The battery worker
// skips I2C reads while dark and samples promptly after the wake key.
bool pdkpass_ui_display_dark(void);

// Refresh the network page when a data service confirms a new update day.
void pdkpass_ui_sync_status_update(void);
