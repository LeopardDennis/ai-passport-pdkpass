#include "pdkpass_tracks.h"

#include <assert.h>
#include <string.h>

// Compare the colors after the 5/6/5 channel precision used by the display.
static unsigned background_distance_squared(uint32_t first, uint32_t second)
{
    int red = (int)((first >> 16) & 0xF8U) - (int)((second >> 16) & 0xF8U);
    int green = (int)((first >> 8) & 0xFCU) - (int)((second >> 8) & 0xFCU);
    int blue = (int)(first & 0xF8U) - (int)(second & 0xF8U);
    return (unsigned)(red * red + green * green + blue * blue);
}

static void assert_track(const char *circuit)
{
    pdkpass_track_geometry_t geometry = { 0 };
    assert(pdkpass_track_get(circuit, &geometry));
    assert(geometry.xy != NULL);
    assert(geometry.point_count == 49U);
    assert(geometry.xy[0] == geometry.xy[(geometry.point_count - 1U) * 2U]);
    assert(geometry.xy[1] == geometry.xy[(geometry.point_count - 1U) * 2U + 1U]);
    for (size_t i = 0; i < geometry.point_count; i++) {
        assert(geometry.xy[i * 2U] <= 191U);
        assert(geometry.xy[i * 2U + 1U] <= 60U);
    }
}

int main(void)
{
    static const char *circuits[] = {
        "MELBOURNE", "SHANGHAI", "SUZUKA", "MIAMI", "MONTREAL", "MONACO",
        "BARCELONA", "SPIELBERG", "SILVERSTONE", "SPA-FRANCORCHAMPS",
        "HUNGARORING", "ZANDVOORT", "MONZA", "MADRING", "BAKU", "SEPANG",
        "MARINA BAY", "COTA", "MEXICO CITY", "INTERLAGOS", "LAS VEGAS",
        "LUSAIL", "YAS MARINA", "PORTIMAO", "ISTANBUL", "SAKHIR", "JEDDAH",
    };
    for (size_t i = 0; i < sizeof(circuits) / sizeof(circuits[0]); i++) {
        assert_track(circuits[i]);
    }

    pdkpass_track_geometry_t madring;
    pdkpass_track_geometry_t madrid;
    assert(pdkpass_track_get("MADRING", &madring));
    assert(pdkpass_track_get("MADRID", &madrid));
    assert(madring.xy == madrid.xy);

    assert(pdkpass_track_count() == 27);
    for (size_t i = 0; i < pdkpass_track_count(); i++) {
        const pdkpass_track_info_t *track = pdkpass_track_at(i);
        assert(track && track->length_m > 3000 && track->length_m < 8000);
        assert(pdkpass_track_find(track->id) == track);
        assert(pdkpass_track_find(track->name) == track);
        assert_track(track->id);
        for (size_t j = 0; j < i; j++) {
            const pdkpass_track_info_t *previous = pdkpass_track_at(j);
            assert(strcmp(track->id, previous->id) != 0);
            assert(track->accent != previous->accent);
            assert(background_distance_squared(track->background,
                                               previous->background) >= 900U);
        }
    }
    assert(!pdkpass_track_at(27));
    assert(!pdkpass_track_find(""));
    assert(!pdkpass_track_find("Portugal")); // Country is not a circuit identity.
    assert(pdkpass_track_find("Portimão") == pdkpass_track_find("PORTIMAO"));
    assert(pdkpass_track_find("Autódromo Internacional do Algarve")->length_m == 4653);
    assert(pdkpass_track_find("Intercity Istanbul Park")->length_m == 5338);
    assert(pdkpass_track_find("Bahrain International Circuit")->length_m == 5412);
    assert(pdkpass_track_find("Jeddah Corniche Circuit")->length_m == 6175);

    pdkpass_track_geometry_t unknown = { 0 };
    assert(!pdkpass_track_get("UNKNOWN", &unknown));
    assert(!pdkpass_track_get(NULL, &unknown));
    assert(!pdkpass_track_get("MONZA", NULL));
    return 0;
}
