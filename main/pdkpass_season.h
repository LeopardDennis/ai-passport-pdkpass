#pragma once

#include "esp_err.h"
#include "pdkpass_data.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint16_t year;
    uint8_t race_count;
    uint8_t driver_count;
    char standings_as_of[12];
    pdkpass_race_t races[PDKPASS_MAX_RACES];
    pdkpass_driver_t drivers[PDKPASS_MAX_DRIVERS];
} pdkpass_season_snapshot_t;

typedef void (*pdkpass_season_callback_t)(void);

// Loads the last complete snapshot; a valid clock selects a newer bundled
// season even offline. Starts the daily OpenF1 calendar/standings synchronizer.
esp_err_t pdkpass_season_start(pdkpass_season_callback_t callback);

// Network state includes time validity so an unset RTC cannot select a bogus
// year. A transition to usable connectivity wakes the synchronizer.
void pdkpass_season_set_network(bool online, bool time_valid);

bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *snapshot);
// True only when the active season has a persisted snapshot, not a bundled calendar.
bool pdkpass_season_has_cached_data(void);
unsigned pdkpass_season_year(void);
size_t pdkpass_season_race_count(void);
size_t pdkpass_season_driver_count(void);
bool pdkpass_season_race_get(size_t index, pdkpass_race_t *race);
bool pdkpass_season_driver_get(size_t index, pdkpass_driver_t *driver);
bool pdkpass_season_driver_by_code(const char *code,
                                   pdkpass_driver_t *driver);
