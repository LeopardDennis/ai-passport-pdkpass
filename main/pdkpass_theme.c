#include "pdkpass_theme.h"
#include "pdkpass_tracks.h"

static uint32_t darken(uint32_t color)
{
    const uint32_t factor = 222U;
    uint32_t red = ((color >> 16) & 0xffU) * factor / 256U;
    uint32_t green = ((color >> 8) & 0xffU) * factor / 256U;
    uint32_t blue = (color & 0xffU) * factor / 256U;
    return (red << 16) | (green << 8) | blue;
}

bool pdkpass_theme_get(const char *circuit, pdkpass_theme_t *theme)
{
    const pdkpass_track_info_t *track = pdkpass_track_find(circuit);
    if (!track || !theme) return false;
    theme->top = track->background;
    theme->bottom = darken(theme->top);
    return true;
}
