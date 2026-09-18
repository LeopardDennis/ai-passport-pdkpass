#include "pdkpass_theme.h"

#include <assert.h>
#include <stddef.h>

int main(void)
{
    static const char *circuits[] = {
        "MELBOURNE", "SHANGHAI", "SUZUKA", "MIAMI", "MONTREAL", "MONACO",
        "BARCELONA", "SPIELBERG", "SILVERSTONE", "SPA-FRANCORCHAMPS",
        "HUNGARORING", "ZANDVOORT", "MONZA", "MADRING", "BAKU", "SEPANG",
        "MARINA BAY", "COTA", "MEXICO CITY", "INTERLAGOS", "LAS VEGAS",
        "LUSAIL", "YAS MARINA", "PORTIMAO", "ISTANBUL", "SAKHIR", "JEDDAH",
    };
    uint32_t colors[sizeof(circuits) / sizeof(circuits[0])] = { 0 };
    for (size_t i = 0; i < sizeof(circuits) / sizeof(circuits[0]); i++) {
        pdkpass_theme_t theme = { 0 };
        assert(pdkpass_theme_get(circuits[i], &theme));
        assert(theme.top != 0U);
        assert(theme.bottom != 0U);
        assert(theme.top != theme.bottom);
        colors[i] = theme.top;
        for (size_t previous = 0; previous < i; previous++) {
            assert(colors[previous] != colors[i]);
        }
    }

    pdkpass_theme_t madrid = { 0 };
    pdkpass_theme_t madring = { 0 };
    assert(pdkpass_theme_get("MADRID", &madrid));
    assert(pdkpass_theme_get("MADRING", &madring));
    assert(madrid.top == madring.top);
    assert(madrid.bottom == madring.bottom);
    assert(pdkpass_theme_get("Portimão", &madrid));
    assert(pdkpass_theme_get("portimao", &madring));
    assert(madrid.top == madring.top);

    pdkpass_theme_t china = { 0 };
    pdkpass_theme_t monza = { 0 };
    pdkpass_theme_t cota = { 0 };
    assert(pdkpass_theme_get("SHANGHAI", &china));
    assert(pdkpass_theme_get("MONZA", &monza));
    assert(pdkpass_theme_get("COTA", &cota));
    assert(china.top == 0xC8102EU);
    assert(monza.top == 0x59636EU);
    assert(cota.top == 0x173F5FU);

    pdkpass_theme_t unknown = { 0 };
    assert(!pdkpass_theme_get("UNKNOWN", &unknown));
    assert(!pdkpass_theme_get(NULL, &unknown));
    assert(!pdkpass_theme_get("MONZA", NULL));
    return 0;
}
