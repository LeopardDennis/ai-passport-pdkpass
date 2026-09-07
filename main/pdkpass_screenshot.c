#include "pdkpass_screenshot.h"
#include "sdkconfig.h"

#include "bsp_display.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include <stdarg.h>

#ifdef CONFIG_PDKPASS_SCREENSHOT
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define SCREENSHOT_COMMAND "FAP_SCREENSHOT_V1"
#define SCREENSHOT_WIDTH 240
#define SCREENSHOT_HEIGHT 320
#define SCREENSHOT_BYTES (SCREENSHOT_WIDTH * SCREENSHOT_HEIGHT * 2)
#define SCREENSHOT_TASK_STACK 4096

static const char *TAG = "pdkpass_capture";
static lv_display_t *s_display;
static bool s_capture_active;
static bool s_capture_failed;
static size_t s_capture_bytes;
static int32_t s_next_row;
static int64_t s_capture_deadline;

static int quiet_log(const char *format, va_list args)
{
    (void)format; (void)args; return 0;
}

static bool write_all(const uint8_t *data, size_t length)
{
    while (length > 0) {
        if (esp_timer_get_time() >= s_capture_deadline) return false;
        int written = usb_serial_jtag_write_bytes(data, length, pdMS_TO_TICKS(20));
        if (written > 0) {
            data += written;
            length -= (size_t)written;
            continue;
        }
        if (written == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        return false;
    }
    return true;
}

// LVGL renders a full invalidated screen into the existing 20-row draw buffer.
// Observing FLUSH_START lets us stream those rows in display order without a
// second full-screen framebuffer, which would exceed this wearable's RAM budget.
static void on_flush_start(lv_event_t *event)
{
    if (!s_capture_active || s_capture_failed) return;

    lv_display_t *display = lv_event_get_target(event);
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(display);
    if (!area || !draw_buf || !draw_buf->data ||
        area->x1 != 0 || area->x2 != SCREENSHOT_WIDTH - 1 ||
        area->y1 != s_next_row || area->y2 >= SCREENSHOT_HEIGHT ||
        draw_buf->header.stride < SCREENSHOT_WIDTH * 2) {
        s_capture_failed = true;
        return;
    }

    const size_t row_bytes = SCREENSHOT_WIDTH * 2;
    const int32_t row_count = area->y2 - area->y1 + 1;
    for (int32_t row = 0; row < row_count; row++) {
        const uint8_t *pixels = draw_buf->data + row * draw_buf->header.stride;
        if (!write_all(pixels, row_bytes)) {
            s_capture_failed = true;
            return;
        }
        s_capture_bytes += row_bytes;
    }
    s_next_row = area->y2 + 1;
}

static void capture_current_screen(void)
{
    if (!bsp_lvgl_lock(2000)) return;

    // The driver API writes binary data directly without newline translation.
    // Restore the original log hook only after draining the queued pixels.
    vprintf_like_t previous_log = esp_log_set_vprintf(quiet_log);
    s_capture_deadline = esp_timer_get_time() + 2000000LL;

    static const char header[] =
        "FAP_SCREENSHOT_V1 240 320 RGB565LE 153600\n";
    s_capture_bytes = 0;
    s_next_row = 0;
    s_capture_failed = !write_all((const uint8_t *)header, sizeof(header) - 1);
    s_capture_active = !s_capture_failed;

    if (s_capture_active) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(s_display);
    }
    s_capture_active = false;
    int64_t remaining_us = s_capture_deadline - esp_timer_get_time();
    TickType_t remaining_ticks = remaining_us > 0
        ? pdMS_TO_TICKS((uint32_t)(remaining_us / 1000LL)) : 0;
    if (usb_serial_jtag_wait_tx_done(remaining_ticks) != ESP_OK) s_capture_failed = true;
    esp_log_set_vprintf(previous_log);
    bsp_lvgl_unlock();

    if (s_capture_failed || s_capture_bytes != SCREENSHOT_BYTES ||
        s_next_row != SCREENSHOT_HEIGHT) {
        ESP_LOGE(TAG, "Screenshot stream incomplete: %u/%u bytes, next row %ld",
                 (unsigned)s_capture_bytes, (unsigned)SCREENSHOT_BYTES,
                 (long)s_next_row);
    }
}

static void screenshot_task(void *arg)
{
    (void)arg;
    char line[48];
    size_t used = 0;

    while (true) {
        uint8_t byte;
        int received = usb_serial_jtag_read_bytes(&byte, 1, portMAX_DELAY);
        if (received != 1) continue;

        if (byte == '\r') continue;
        if (byte == '\n') {
            line[used] = '\0';
            if (strcmp(line, SCREENSHOT_COMMAND) == 0) {
                capture_current_screen();
            }
            used = 0;
        } else if (used + 1 < sizeof(line)) {
            line[used++] = (char)byte;
        } else {
            used = 0;
        }
    }
}

esp_err_t pdkpass_screenshot_start(lv_display_t *display)
{
    if (!display) return ESP_ERR_INVALID_ARG;
    if (s_display) return ESP_ERR_INVALID_STATE;

    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 1024, .rx_buffer_size = 256};
    esp_err_t err = usb_serial_jtag_driver_install(&usb);
    if (err != ESP_OK) return err;
    if (!bsp_lvgl_lock(2000)) {
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_TIMEOUT;
    }
    s_display = display;
    lv_display_add_event_cb(display, on_flush_start, LV_EVENT_FLUSH_START, NULL);
    bsp_lvgl_unlock();
    BaseType_t created = xTaskCreate(screenshot_task, "fap_capture",
                                     SCREENSHOT_TASK_STACK, NULL, 3, NULL);
    if (created != pdPASS) {
        if (bsp_lvgl_lock(2000)) {
            lv_display_remove_event_cb_with_user_data(display, on_flush_start, NULL);
            bsp_lvgl_unlock();
        }
        s_display = NULL;
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else
esp_err_t pdkpass_screenshot_start(lv_display_t *display)
{
    return display ? ESP_OK : ESP_ERR_INVALID_ARG;
}
#endif
