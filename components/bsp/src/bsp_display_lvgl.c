// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    pc.task_max_sleep_ms = 60000;
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM(总共约 150KB)。
        // 20 行单缓冲 ≈ 9.6KB;若改成 40 行双缓冲(≈37.5KB)会把 I2S 等外设的
        // DMA 描述符挤到 NO_MEM。刷新略慢但稳。
        .buffer_size   = (uint32_t)BSP_LCD_W * 20,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void) {
    lvgl_port_unlock();
    lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
}

static uint32_t s_tick_offset;
static bool s_drawing = true;
static bool s_draw_guard_added;
static void drawing_guard(lv_event_t *event)
{
    if (!s_drawing) {
        lv_display_t *display = lv_event_get_target(event);
        lv_timer_pause(lv_display_get_refr_timer(display));
    }
}
static uint32_t monotonic_tick(void) { return (uint32_t)(esp_timer_get_time() / 1000) + s_tick_offset; }

esp_err_t bsp_lvgl_use_monotonic_clock(void)
{
    // The port creates its tick timer just after announcing initialization.
    // Retry from the application worker if that race returns INVALID_STATE.
    esp_err_t err = lvgl_port_stop();
    if (err == ESP_OK) {
        s_tick_offset = lv_tick_get() - (uint32_t)(esp_timer_get_time() / 1000);
        lv_tick_set_cb(monotonic_tick);
    }
    lv_timer_enable(true);
    return err;
}

void bsp_lvgl_set_drawing(bool enabled)
{
    if (!s_disp || s_drawing == enabled) return;
    if (!s_draw_guard_added) {
        lv_display_add_event_cb(s_disp, drawing_guard, LV_EVENT_REFR_REQUEST, NULL);
        s_draw_guard_added = true;
    }
    s_drawing = enabled;
    lv_display_enable_invalidation(s_disp, enabled);
    lv_timer_t *refresh = lv_display_get_refr_timer(s_disp);
    if (enabled) {
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        lv_timer_resume(refresh);
        lv_timer_ready(refresh);
    } else lv_timer_pause(refresh);
}
