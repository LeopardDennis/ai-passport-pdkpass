#pragma once

#include "pdkpass_results_core.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PDKPASS_PAGE_HOME = 0,
    PDKPASS_PAGE_CALENDAR,
    PDKPASS_PAGE_STANDINGS,
    PDKPASS_PAGE_TEAM_STANDINGS,
    PDKPASS_PAGE_RACE_DETAIL,
    PDKPASS_PAGE_RESULTS,
    PDKPASS_PAGE_NETWORK,
    PDKPASS_PAGE_NETWORK_PROGRESS,
    PDKPASS_PAGE_NETWORK_CONFIRM,
} pdkpass_page_t;

typedef enum {
    PDKPASS_INPUT_UP = 0,
    PDKPASS_INPUT_DOWN,
    PDKPASS_INPUT_OK,
    PDKPASS_INPUT_BACK,
    PDKPASS_INPUT_UP_LONG,
    PDKPASS_INPUT_DOWN_LONG,
} pdkpass_input_t;

#define PDKPASS_DETAIL_ROWS 3U

// One slot per session kind. Unknown timestamps stay zero; a cached result is
// independent evidence of completion even after a restart without a valid clock.
typedef struct {
    int64_t start_utc, end_utc;
    bool cancelled, result_ready;
} pdkpass_detail_session_t;

typedef struct {
    pdkpass_page_t page;
    pdkpass_page_t detail_origin;
    size_t selected_race;
    size_t selected_driver;
    size_t selected_team;
    pdkpass_session_kind_t selected_session;
    uint32_t session_mask; // Visible sessions for the selected round.
    pdkpass_session_kind_t detail_sessions[PDKPASS_SESSION_COUNT];
    size_t detail_count;
    pdkpass_session_kind_t detail_selected_session;
    // Current weekend stays separate from a manually browsed home round.
    size_t home_race;
    bool home_browsing;
    bool season_complete;
    unsigned network_selection;
} pdkpass_state_t;

void pdkpass_state_init(pdkpass_state_t *state);
void pdkpass_state_set_home_race(pdkpass_state_t *state, size_t race_index,
                                 size_t race_count);
void pdkpass_state_reset_home_race(pdkpass_state_t *state, size_t race_count);
// An empty mask selects PDKPASS_SESSION_COUNT; navigation remains bounded.
void pdkpass_state_set_sessions(pdkpass_state_t *state, uint32_t mask);
// Sort known start times, retain conventional slots for unknown times, and only
// choose a default on entry. Live updates retain the user's visible selection.
void pdkpass_state_set_detail_sessions(pdkpass_state_t *state, uint32_t mask,
    const pdkpass_detail_session_t sessions[PDKPASS_SESSION_COUNT],
    bool choose_default, bool time_valid, int64_t now_utc);
size_t pdkpass_state_detail_index(const pdkpass_state_t *state);
void pdkpass_state_handle(pdkpass_state_t *state, pdkpass_input_t input,
                          size_t race_count, size_t driver_count, size_t team_count);
