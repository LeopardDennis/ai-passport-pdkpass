#include "pdkpass_sound_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static unsigned crossings(const int16_t *pcm, size_t start, size_t count)
{
    unsigned changes = 0;
    int previous = 0;
    for (size_t i = start; i < start + count; i++) {
        int sign = (pcm[i] > 0) - (pcm[i] < 0);
        if (sign != 0 && previous != 0 && sign != previous) changes++;
        if (sign != 0) previous = sign;
    }
    return changes;
}

int main(void)
{
    int16_t pcm[PDKPASS_SOUND_SAMPLES];
    assert(pdkpass_sound_render(PDKPASS_SOUND_UP, NULL,
                                PDKPASS_SOUND_SAMPLES) == 0U);
    assert(pdkpass_sound_render(PDKPASS_SOUND_UP, pcm,
                                PDKPASS_SOUND_SAMPLES - 1U) == 0U);
    assert(pdkpass_sound_render((pdkpass_sound_kind_t)99, pcm,
                                PDKPASS_SOUND_SAMPLES) == 0U);
    for (pdkpass_sound_kind_t kind = PDKPASS_SOUND_UP;
         kind <= PDKPASS_SOUND_BACK; kind++) {
        assert(pdkpass_sound_render(kind, pcm, PDKPASS_SOUND_SAMPLES) ==
               PDKPASS_SOUND_SAMPLES);
        assert(pcm[0] == 0);
        assert(pcm[PDKPASS_SOUND_NOTE_SAMPLES - 1U] == 0);
        assert(pcm[PDKPASS_SOUND_SAMPLES - 1U] == 0);
        bool audible = false;
        for (size_t i = 0; i < PDKPASS_SOUND_SAMPLES; i++) {
            assert(pcm[i] >= -5200 && pcm[i] <= 5200);
            if (pcm[i] != 0) audible = true;
            if (i >= PDKPASS_SOUND_NOTE_SAMPLES &&
                i < PDKPASS_SOUND_NOTE_SAMPLES + PDKPASS_SOUND_GAP_SAMPLES)
                assert(pcm[i] == 0);
            if (i >= PDKPASS_SOUND_SAMPLES - PDKPASS_SOUND_TAIL_SAMPLES)
                assert(pcm[i] == 0);
        }
        assert(audible);
    }
    assert(pdkpass_sound_render(PDKPASS_SOUND_UP, pcm,
                                PDKPASS_SOUND_SAMPLES) == PDKPASS_SOUND_SAMPLES);
    unsigned first = crossings(pcm, 0, PDKPASS_SOUND_NOTE_SAMPLES);
    unsigned second = crossings(pcm,
        PDKPASS_SOUND_NOTE_SAMPLES + PDKPASS_SOUND_GAP_SAMPLES,
        PDKPASS_SOUND_NOTE_SAMPLES);
    assert(second > first);
    assert(pdkpass_sound_render(PDKPASS_SOUND_DOWN, pcm,
                                PDKPASS_SOUND_SAMPLES) == PDKPASS_SOUND_SAMPLES);
    first = crossings(pcm, 0, PDKPASS_SOUND_NOTE_SAMPLES);
    second = crossings(pcm,
        PDKPASS_SOUND_NOTE_SAMPLES + PDKPASS_SOUND_GAP_SAMPLES,
        PDKPASS_SOUND_NOTE_SAMPLES);
    assert(first > second);
    puts("Button sound cues: PASS");
    return 0;
}
