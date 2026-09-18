#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *xy;
    size_t point_count;
} pdkpass_track_geometry_t;

// A circuit/layout is independent of a season or race weekend. Laps and
// dates belong to the race, not this catalog. IDs remain stable across years.
typedef struct {
    const char *id;
    const char *name;
    uint16_t length_m;
    uint32_t accent;
    uint32_t background;
    pdkpass_track_geometry_t geometry;
} pdkpass_track_info_t;

size_t pdkpass_track_count(void);
const pdkpass_track_info_t *pdkpass_track_at(size_t index);
// Accepts a stable ID or a known circuit alias; never matches by country.
const pdkpass_track_info_t *pdkpass_track_find(const char *circuit);

bool pdkpass_track_get(const char *circuit,
                       pdkpass_track_geometry_t *geometry);
