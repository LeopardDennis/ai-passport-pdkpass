#include "pdkpass_sound.h"

#include "bsp_audio.h"
#include "pdkpass_sound_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdatomic.h>

#define SOUND_IDLE_MS 250U
#define SOUND_VOLUME 50U

static const char *TAG = "pdkpass_sound";
static QueueHandle_t s_queue;
static int16_t s_pcm[PDKPASS_SOUND_SAMPLES];
static const int16_t s_reminder_pcm[] = {
#include "../assets/music/session_reminder_pcm.inc"
};
_Static_assert(sizeof(s_reminder_pcm) / sizeof(s_reminder_pcm[0]) == 48000,
               "Reminder must be the approved three-second recording");
static atomic_bool s_reminder_active;
static atomic_bool s_reminder_cancelled = true;
static int s_consumed_button = -1; // Owned by the button callback.

void pdkpass_sound_reminder_play(void)
{
    atomic_store(&s_reminder_cancelled, false);
    atomic_store(&s_reminder_active, true);
    if (s_queue) {
        pdkpass_sound_kind_t kind = PDKPASS_SOUND_REMINDER;
        xQueueOverwrite(s_queue, &kind);
    }
}

void pdkpass_sound_reminder_stop(void)
{
    atomic_store(&s_reminder_active, false);
    atomic_store(&s_reminder_cancelled, true);
}

bool pdkpass_sound_consume_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_PRESS) {
        s_consumed_button = -1;
        if (atomic_exchange(&s_reminder_active, false)) {
            atomic_store(&s_reminder_cancelled, true);
            s_consumed_button = (int)button;
        }
    }
    return s_consumed_button == (int)button;
}

static void play_reminder(void)
{
    bsp_audio_set_volume(80);
    for (size_t offset = 0; offset < 48000U &&
         !atomic_load(&s_reminder_cancelled); offset += 256U) {
        size_t count = 48000U - offset;
        if (count > 256U) count = 256U;
        if (bsp_audio_write(s_reminder_pcm + offset, count * sizeof(int16_t)) != ESP_OK)
            break;
    }
    // Drain the last DMA block, unless a key requested immediate silence.
    if (!atomic_load(&s_reminder_cancelled)) vTaskDelay(pdMS_TO_TICKS(100));
    bsp_audio_stop();
    bsp_audio_set_volume(SOUND_VOLUME);
}

static bool open_sound(void)
{
    if (bsp_audio_set_format(PDKPASS_SOUND_SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Button sound unavailable: codec open failed");
        return false;
    }
    bsp_audio_set_volume(SOUND_VOLUME);
    return true;
}

static void sound_worker(void *unused)
{
    (void)unused;
    // Prepare on this worker at startup, not on the first button press.
    bool available = bsp_audio_init() == ESP_OK;
    if (!available) ESP_LOGW(TAG, "Button sound unavailable: audio init failed");
    bool opened = available && open_sound();
    pdkpass_sound_kind_t kind;
    for (;;) {
        TickType_t wait = opened ? pdMS_TO_TICKS(SOUND_IDLE_MS) : portMAX_DELAY;
        if (xQueueReceive(s_queue, &kind, wait) != pdTRUE) {
            if (opened) bsp_audio_stop();
            opened = false;
            continue;
        }
        if (!available) continue;
        if (!opened) {
            opened = open_sound();
            if (!opened) continue;
        }
        // A newer press during codec startup supersedes an old pending cue.
        pdkpass_sound_kind_t latest;
        if (xQueueReceive(s_queue, &latest, 0) == pdTRUE) kind = latest;
        if (kind == PDKPASS_SOUND_REMINDER) {
            if (!atomic_load(&s_reminder_cancelled)) play_reminder();
            else bsp_audio_stop();
            opened = false;
            continue;
        }
        size_t count = pdkpass_sound_render(kind, s_pcm, PDKPASS_SOUND_SAMPLES);
        if (count > 0U) {
            if (bsp_audio_write(s_pcm, count * sizeof(s_pcm[0])) == ESP_OK) {
                // Allow queued PCM to finish; keep the codec warm for nearby
                // presses, then close it after the bounded idle interval.
                vTaskDelay(pdMS_TO_TICKS(70));
            } else {
                ESP_LOGW(TAG, "Button sound playback failed");
                bsp_audio_stop();
                opened = false;
            }
        }
    }
}

esp_err_t pdkpass_sound_start(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(1, sizeof(pdkpass_sound_kind_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_worker, "pdk_sfx", 4096, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_sound_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (atomic_load(&s_reminder_active) || !s_queue || (event != BSP_BTN_PRESS &&
                     !(event == BSP_BTN_LONG && button == BSP_BTN_OK))) return;
    pdkpass_sound_kind_t kind;
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK)
        kind = PDKPASS_SOUND_BACK;
    else if (button == BSP_BTN_UP) kind = PDKPASS_SOUND_UP;
    else if (button == BSP_BTN_DOWN) kind = PDKPASS_SOUND_DOWN;
    else kind = PDKPASS_SOUND_OK;
    xQueueOverwrite(s_queue, &kind);
}

void pdkpass_sound_result_ready(void)
{
    if (atomic_load(&s_reminder_active) || !s_queue) return;
    pdkpass_sound_kind_t kind = PDKPASS_SOUND_RESULT_READY;
    // Do not replace a reminder (or a key cue) that is already queued.
    xQueueSend(s_queue, &kind, 0);
}
