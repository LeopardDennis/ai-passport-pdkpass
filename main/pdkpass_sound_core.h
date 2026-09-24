#pragma once

#include <stddef.h>
#include <stdint.h>

#define PDKPASS_SOUND_SAMPLE_RATE 16000U
#define PDKPASS_SOUND_NOTE_SAMPLES 400U
#define PDKPASS_SOUND_GAP_SAMPLES 64U
#define PDKPASS_SOUND_TAIL_SAMPLES 240U
#define PDKPASS_SOUND_SAMPLES (PDKPASS_SOUND_NOTE_SAMPLES * 2U + \
                              PDKPASS_SOUND_GAP_SAMPLES + PDKPASS_SOUND_TAIL_SAMPLES)

typedef enum {
    PDKPASS_SOUND_UP,
    PDKPASS_SOUND_DOWN,
    PDKPASS_SOUND_OK,
    PDKPASS_SOUND_BACK,
} pdkpass_sound_kind_t;

// Render one short, faded two-note PCM cue. Returns zero for invalid inputs.
size_t pdkpass_sound_render(pdkpass_sound_kind_t kind, int16_t *pcm,
                            size_t capacity);
