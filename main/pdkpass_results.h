#pragma once

#include "esp_err.h"
#include "pdkpass_results_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PDKPASS_PODIUM_SIZE 3

typedef enum {
    PDKPASS_RESULT_UNKNOWN = 0,
    PDKPASS_RESULT_NOT_HELD,
    PDKPASS_RESULT_SCHEDULED,
    PDKPASS_RESULT_CANCELLED,
    PDKPASS_RESULT_READY,
} pdkpass_result_status_t;

typedef struct {
    unsigned position;
    char code[4];
    char name[16];
    char team[16];
} pdkpass_podium_driver_t;

typedef struct {
    pdkpass_result_status_t status;
    int64_t session_end_utc;
    pdkpass_podium_driver_t podium[PDKPASS_PODIUM_SIZE];
} pdkpass_result_snapshot_t;

typedef void (*pdkpass_results_callback_t)(size_t race_index);

// Load cached results and start the background OpenF1 historical-results
// worker. The worker never stores API credentials and only requests sessions
// that ended at least 30 minutes earlier.
esp_err_t pdkpass_results_start(pdkpass_results_callback_t callback);

// Called by the Wi-Fi service whenever usable internet connectivity changes.
void pdkpass_results_set_online(bool online);

// Reset the runtime view when the season service atomically adopts another
// year/calendar, then load only a cache whose year and meetings still match.
void pdkpass_results_season_changed(void);

// Prioritize a round because the user opened its results page.
void pdkpass_results_request_race(size_t race_index);

// Copy one thread-safe result snapshot for rendering.
bool pdkpass_results_get(size_t race_index, pdkpass_session_kind_t session,
                         pdkpass_result_snapshot_t *snapshot);
// True when the active season has persisted session/discovery information.
bool pdkpass_results_has_cached_data(void);
