#pragma once
#include "pdkpass_reminder_core.h"
#include "esp_err.h"

// No additional task or network polling. poll/wait run on the existing UI I/O
// worker, outside the LVGL lock. HTTP workers publish complete round schedules.
esp_err_t pdkpass_reminder_init(void (*wake_worker)(void));
void pdkpass_reminder_set_time_valid(bool valid);
void pdkpass_reminder_season_changed(void);
void pdkpass_reminder_update_round(unsigned year, unsigned round, int32_t meeting_key,
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT]);
bool pdkpass_reminder_poll(int64_t now, pdkpass_reminder_entry_t *alert);
uint32_t pdkpass_reminder_wait_ms(int64_t now);
// These settings calls are non-blocking and may run under the LVGL lock.
bool pdkpass_reminder_enabled(void);
void pdkpass_reminder_set_enabled(bool enabled);

// Copy a discovered round schedule regardless of whether reminders are enabled.
// Non-blocking under the LVGL lock; false means no matching schedule available.
bool pdkpass_reminder_round_schedule(unsigned year, int32_t meeting_key,
    pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT]);
