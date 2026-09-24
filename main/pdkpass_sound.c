#include "pdkpass_sound.h"

#include "bsp_audio.h"
#include "pdkpass_sound_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char *TAG = "pdkpass_sound";
static QueueHandle_t s_queue;
static int16_t s_pcm[PDKPASS_SOUND_SAMPLES];

static void sound_worker(void *unused)
{
    (void)unused;
    bool initialized = false;
    bool available = true;
    pdkpass_sound_kind_t kind;
    for (;;) {
        if (xQueueReceive(s_queue, &kind, portMAX_DELAY) != pdTRUE) continue;
        if (!available) continue;
        if (!initialized) {
            if (bsp_audio_init() != ESP_OK) {
                ESP_LOGW(TAG, "Button sound unavailable: audio init failed");
                available = false;
                continue;
            }
            initialized = true;
        }
        if (bsp_audio_set_format(PDKPASS_SOUND_SAMPLE_RATE, 16, 1) != ESP_OK) {
            ESP_LOGW(TAG, "Button sound unavailable: codec open failed");
            available = false;
            continue;
        }
        bsp_audio_set_volume(30);
        size_t count = pdkpass_sound_render(kind, s_pcm,
                                           PDKPASS_SOUND_SAMPLES);
        if (count > 0U) {
            if (bsp_audio_write(s_pcm, count * sizeof(s_pcm[0])) == ESP_OK) {
                // write may only fill DMA; let the queued samples finish
                // before closing the codec and stopping I2S clocks.
                vTaskDelay(pdMS_TO_TICKS(70));
            } else {
                ESP_LOGW(TAG, "Button sound playback failed");
            }
        }
        bsp_audio_stop();
    }
}

esp_err_t pdkpass_sound_start(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(1, sizeof(pdkpass_sound_kind_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_worker, "pdk_sfx", 4096, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_sound_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_queue || (event != BSP_BTN_CLICK && event != BSP_BTN_LONG)) return;
    pdkpass_sound_kind_t kind;
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK)
        kind = PDKPASS_SOUND_BACK;
    else if (button == BSP_BTN_UP) kind = PDKPASS_SOUND_UP;
    else if (button == BSP_BTN_DOWN) kind = PDKPASS_SOUND_DOWN;
    else kind = PDKPASS_SOUND_OK;
    xQueueOverwrite(s_queue, &kind);
}
