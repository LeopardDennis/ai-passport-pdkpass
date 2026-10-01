#include "pdkpass_cache.h"
// PDKPASS application entry point for FoloToy AI Passport.
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "pdkpass_network.h"
#include "pdkpass_power.h"
#include "pdkpass_results.h"
#include "pdkpass_reminder.h"
#include <time.h>
#include "pdkpass_season.h"
#include "pdkpass_sound.h"
#include "pdkpass_sync_policy.h"
#include "pdkpass_ui.h"
#include "pdkpass_ui_updates.h"

static const char *TAG = "pdkpass";

#define BATTERY_LIT_POLL_MS 60000U

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; bool reschedule; } key_event_t;
static QueueHandle_t s_keys;
static portMUX_TYPE s_updates_lock = portMUX_INITIALIZER_UNLOCKED;
static pdkpass_ui_updates_t s_updates;
static void wake_reminder_worker(void);

static void on_network(const pdkpass_network_update_t *update)
{
    pdkpass_season_set_network(update->state == PDKPASS_NETWORK_ONLINE,
                               update->time_valid);
    pdkpass_results_set_online(update->state == PDKPASS_NETWORK_ONLINE);
    pdkpass_reminder_set_time_valid(update->time_valid);
    portENTER_CRITICAL(&s_updates_lock);
    pdkpass_ui_updates_network(&s_updates, update);
    portEXIT_CRITICAL(&s_updates_lock);
    wake_reminder_worker();
}

static void on_season(void)
{
    pdkpass_results_season_changed();
    pdkpass_reminder_season_changed();
    portENTER_CRITICAL(&s_updates_lock);
    s_updates.season = true;
    portEXIT_CRITICAL(&s_updates_lock);
    wake_reminder_worker();
}

static void on_results(size_t race_index, bool new_result)
{
    if (new_result) pdkpass_sound_result_ready();
    if (race_index >= PDKPASS_MAX_RACES) return;
    portENTER_CRITICAL(&s_updates_lock);
    s_updates.races |= UINT32_C(1) << race_index;
    portEXIT_CRITICAL(&s_updates_lock);
    wake_reminder_worker();
}

static void on_data_status(void)
{
    portENTER_CRITICAL(&s_updates_lock);
    s_updates.status = true;
    portEXIT_CRITICAL(&s_updates_lock);
    wake_reminder_worker();
}

static void wake_reminder_worker(void)
{
    if (!s_keys) return;
    key_event_t event = {.reschedule = true};
    xQueueSend(s_keys, &event, 0);
}

static bool ui_updates_pending(void)
{
    portENTER_CRITICAL(&s_updates_lock);
    bool pending = pdkpass_ui_updates_pending(&s_updates);
    portEXIT_CRITICAL(&s_updates_lock);
    return pending;
}

static void dispatch_ui_updates(void)
{
    if (!ui_updates_pending() || !bsp_lvgl_lock(500)) return;
    pdkpass_ui_updates_t updates;
    portENTER_CRITICAL(&s_updates_lock);
    pdkpass_ui_updates_take(&s_updates, &updates);
    portEXIT_CRITICAL(&s_updates_lock);
    if (updates.network) pdkpass_ui_network_update(&updates.update);
    if (updates.season && !pdkpass_ui_season_update()) {
        portENTER_CRITICAL(&s_updates_lock);
        s_updates.season = true;
        portEXIT_CRITICAL(&s_updates_lock);
    }
    for (size_t i = 0; i < PDKPASS_MAX_RACES; ++i)
        if (updates.races & (UINT32_C(1) << i)) pdkpass_ui_results_update(i);
    if (updates.status) pdkpass_ui_sync_status_update();
    bsp_lvgl_unlock();
}

typedef enum {
    BATTERY_SAMPLE_RETRY,
    BATTERY_SAMPLE_PAUSE_DARK,
    BATTERY_SAMPLE_DONE,
} battery_sample_outcome_t;

static battery_sample_outcome_t sample_battery_if_visible(bool available)
{
    if (!bsp_lvgl_lock(500)) return BATTERY_SAMPLE_RETRY;
    bool dark = pdkpass_ui_display_dark();
    bsp_lvgl_unlock();
    if (dark) return BATTERY_SAMPLE_PAUSE_DARK;

    int soc = available ? bsp_battery_soc() : -1;
    if (!bsp_lvgl_lock(500)) return BATTERY_SAMPLE_RETRY;
    pdkpass_ui_battery_update(soc);
    bsp_lvgl_unlock();
    return BATTERY_SAMPLE_DONE;
}

// Sampling and button dispatch share one small worker. Callbacks only enqueue;
// slow I2C never runs in the LVGL timer or button driver's context.
static void ui_worker(void *arg)
{
    bool battery_available = (bool)(uintptr_t)arg;
    TickType_t next_battery = xTaskGetTickCount();
    bool battery_paused_for_dark = false;
    bool monotonic_ready = false;
    bool alert_pending = false;
    pdkpass_reminder_entry_t alert;
    for (;;) {
        dispatch_ui_updates();
        if (!monotonic_ready && bsp_lvgl_lock(500)) {
            monotonic_ready = bsp_lvgl_use_monotonic_clock() == ESP_OK;
            bsp_lvgl_unlock();
        }
        if (!alert_pending) alert_pending = pdkpass_reminder_poll((int64_t)time(NULL), &alert);
        if (alert_pending && bsp_lvgl_lock(500)) {
            if (alert.start_utc > (int64_t)time(NULL)) pdkpass_ui_reminder_show(&alert);
            bsp_lvgl_unlock();
            alert_pending = false;
            battery_paused_for_dark = false;
            next_battery = xTaskGetTickCount();
        }
        TickType_t now = xTaskGetTickCount();
        if (!battery_paused_for_dark &&
            (int32_t)(now - next_battery) >= 0) {
            battery_sample_outcome_t outcome =
                sample_battery_if_visible(battery_available);
            if (outcome == BATTERY_SAMPLE_PAUSE_DARK) {
                // Keys and scheduled reminders can wake the dark display.
                battery_paused_for_dark = true;
            } else if (outcome == BATTERY_SAMPLE_RETRY) {
                next_battery = now + pdMS_TO_TICKS(1000);
            } else {
                next_battery = xTaskGetTickCount() +
                               pdMS_TO_TICKS(BATTERY_LIT_POLL_MS);
            }
        }
        key_event_t key;
        now = xTaskGetTickCount();
        TickType_t wait = battery_paused_for_dark ? portMAX_DELAY :
            ((int32_t)(next_battery - now) > 0 ? next_battery - now : 1);
        uint32_t reminder_ms = pdkpass_reminder_wait_ms((int64_t)time(NULL));
        if (reminder_ms != UINT32_MAX && pdMS_TO_TICKS(reminder_ms) < wait)
            wait = pdMS_TO_TICKS(reminder_ms);
        if (!wait) wait = 1;
        if (ui_updates_pending() && wait > pdMS_TO_TICKS(100))
            wait = pdMS_TO_TICKS(100);
        if (alert_pending && wait > pdMS_TO_TICKS(100)) wait = pdMS_TO_TICKS(100);
        if (!monotonic_ready && wait > pdMS_TO_TICKS(1000))
            wait = pdMS_TO_TICKS(1000);
        if (xQueueReceive(s_keys, &key, wait) == pdTRUE && !key.reschedule && bsp_lvgl_lock(500)) {
            bool waking = pdkpass_ui_display_dark();
            if (key.event == BSP_BTN_PRESS) pdkpass_ui_reminder_dismiss();
            else pdkpass_ui_key(key.button, key.event);
            bsp_lvgl_unlock();
            if (waking) {
                battery_paused_for_dark = false;
                next_battery = xTaskGetTickCount();
            }
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (pdkpass_sound_consume_key(btn, ev)) {
        if (s_keys && ev == BSP_BTN_PRESS) {
            key_event_t key = {.button = btn, .event = ev};
            xQueueSend(s_keys, &key, 0);
        }
        return;
    }
    // Sound only enqueues here: it must not wait for click detection or UI work.
    pdkpass_sound_key(btn, ev);
    if (!s_keys || (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG)) return;
    key_event_t key = {.button = btn, .event = ev};
    xQueueSend(s_keys, &key, 0);
}

void app_main(void)
{
    esp_err_t power_err = pdkpass_power_init();
    if (power_err != ESP_OK) ESP_LOGW(TAG, "Power management unavailable: %s", esp_err_to_name(power_err));

    esp_err_t cache_err = pdkpass_cache_init();
    if (cache_err != ESP_OK) {
        ESP_LOGE(TAG, "Application storage initialization/reset failed: %s",
                 esp_err_to_name(cache_err));
        return; // No service may load old Wi-Fi/settings after a failed reset.
    }
    pdkpass_sync_status_init(on_data_status);
    if (pdkpass_reminder_init(wake_reminder_worker) != ESP_OK)
        ESP_LOGW(TAG, "Session reminders unavailable");

    if (pdkpass_season_start(on_season) != ESP_OK) {
        ESP_LOGE(TAG, "Season service failed to start");
    }

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG,
                 "Display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // Battery support is optional; unavailable readings use an empty outline.
    bool battery_available = bsp_battery_init() == ESP_OK;
    if (bsp_lvgl_lock(1000)) {
        pdkpass_ui_enter(battery_available);
        bsp_lvgl_unlock();
    }

    s_keys = xQueueCreate(12, sizeof(key_event_t));
    if (!s_keys || xTaskCreate(ui_worker, "pdk_ui_io", 3072,
                              (void *)(uintptr_t)battery_available, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "UI worker failed to start");
    }
    if (pdkpass_sound_start() != ESP_OK) {
        ESP_LOGW(TAG, "Button sounds unavailable");
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed; the current screen remains readable");
    }

    if (pdkpass_results_start(on_results) != ESP_OK) {
        ESP_LOGE(TAG, "Session results service failed to start");
    }

    if (pdkpass_network_start(on_network) != ESP_OK) {
        ESP_LOGE(TAG, "Network time service failed to start");
        const pdkpass_network_update_t failed = {.state = PDKPASS_NETWORK_OFFLINE};
        on_network(&failed);
    }

}
