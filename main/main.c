// PDKPASS application entry point for FoloToy AI Passport.
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "pdkpass_network.h"
#include "pdkpass_results.h"
#include "pdkpass_screenshot.h"
#include "pdkpass_season.h"
#include "pdkpass_ui.h"

static const char *TAG = "pdkpass";

static void on_network(const pdkpass_network_update_t *update)
{
    pdkpass_season_set_network(update->state == PDKPASS_NETWORK_ONLINE,
                               update->time_valid);
    pdkpass_results_set_online(update->state == PDKPASS_NETWORK_ONLINE);
    if (!bsp_lvgl_lock(500)) return;
    pdkpass_ui_network_update(update);
    bsp_lvgl_unlock();
}

static void on_season(void)
{
    pdkpass_results_season_changed();
    if (!bsp_lvgl_lock(500)) return;
    pdkpass_ui_season_update();
    bsp_lvgl_unlock();
}

static void on_results(size_t race_index)
{
    if (!bsp_lvgl_lock(500)) return;
    pdkpass_ui_results_update(race_index);
    bsp_lvgl_unlock();
}

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_event_t;
static QueueHandle_t s_keys;

// Sampling and button dispatch share one small worker. Callbacks only enqueue;
// slow I2C never runs in the LVGL timer or button driver's context.
static void ui_worker(void *arg)
{
    bool battery_available = (bool)(uintptr_t)arg;
    TickType_t next_battery = xTaskGetTickCount();
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_battery) >= 0) {
            int soc = battery_available ? bsp_battery_soc() : -1;
            if (bsp_lvgl_lock(500)) {
                pdkpass_ui_battery_update(soc);
                bsp_lvgl_unlock();
            }
            next_battery = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
        }
        key_event_t key;
        now = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next_battery - now) > 0 ? next_battery - now : 1;
        if (xQueueReceive(s_keys, &key, wait) == pdTRUE && bsp_lvgl_lock(500)) {
            pdkpass_ui_key(key.button, key.event);
            bsp_lvgl_unlock();
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_keys || (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG)) return;
    key_event_t key = {.button = btn, .event = ev};
    xQueueSend(s_keys, &key, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "PDKPASS starting");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed without erase: %s",
                 esp_err_to_name(nvs_err));
    }

    if (pdkpass_season_start(on_season) != ESP_OK) {
        ESP_LOGE(TAG, "Season service failed to start");
    }

    lv_display_t *display = NULL;
    if (bsp_display_init() != ESP_OK || !(display = bsp_lvgl_init())) {
        ESP_LOGE(TAG,
                 "Display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // Battery support is optional. The UI renders BAT -- when the fuel gauge is
    // not installed or cannot be read.
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
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed; the current screen remains readable");
    }

    if (pdkpass_screenshot_start(display) != ESP_OK) {
        ESP_LOGE(TAG, "Release screenshot service failed to start");
    }

    if (pdkpass_results_start(on_results) != ESP_OK) {
        ESP_LOGE(TAG, "Session results service failed to start");
    }

    if (pdkpass_network_start(on_network) != ESP_OK) {
        ESP_LOGE(TAG, "Network time service failed to start");
    }

    ESP_LOGI(TAG, "PDKPASS ready; battery=%d", battery_available);
}
