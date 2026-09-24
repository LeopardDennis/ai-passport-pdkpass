#include "pdkpass_sound_core.h"

#include <string.h>

#define TONE_AMPLITUDE 5200
#define ATTACK_SAMPLES 32U
#define RELEASE_SAMPLES 64U

size_t pdkpass_sound_render(pdkpass_sound_kind_t kind, int16_t *pcm,
                            size_t capacity)
{
    static const uint16_t notes[][2] = {
        [PDKPASS_SOUND_UP] = { 880, 1175 },
        [PDKPASS_SOUND_DOWN] = { 1175, 880 },
        [PDKPASS_SOUND_OK] = { 988, 1480 },
        [PDKPASS_SOUND_BACK] = { 740, 523 },
    };
    if (!pcm || capacity < PDKPASS_SOUND_SAMPLES ||
        (unsigned)kind >= sizeof(notes) / sizeof(notes[0])) return 0;

    memset(pcm, 0, PDKPASS_SOUND_SAMPLES * sizeof(*pcm));
    for (size_t note = 0; note < 2U; note++) {
        uint32_t phase = 0;
        size_t offset = note * (PDKPASS_SOUND_NOTE_SAMPLES +
                                PDKPASS_SOUND_GAP_SAMPLES);
        for (size_t i = 0; i < PDKPASS_SOUND_NOTE_SAMPLES; i++) {
            phase += notes[kind][note];
            if (phase >= PDKPASS_SOUND_SAMPLE_RATE)
                phase -= PDKPASS_SOUND_SAMPLE_RATE;
            unsigned gain = 256U;
            if (i < ATTACK_SAMPLES) gain = (unsigned)(i * 256U / ATTACK_SAMPLES);
            size_t remaining = PDKPASS_SOUND_NOTE_SAMPLES - 1U - i;
            if (remaining < RELEASE_SAMPLES) {
                unsigned release = (unsigned)(remaining * 256U / RELEASE_SAMPLES);
                if (release < gain) gain = release;
            }
            int amplitude = TONE_AMPLITUDE * (int)gain / 256;
            pcm[offset + i] = phase < PDKPASS_SOUND_SAMPLE_RATE / 2U
                                  ? (int16_t)amplitude : (int16_t)-amplitude;
        }
    }
    return PDKPASS_SOUND_SAMPLES;
}
