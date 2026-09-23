#include "pdkpass_model.h"

static size_t wrap_previous(size_t value, size_t count)
{
    return count == 0 ? 0 : (value + count - 1) % count;
}

static size_t wrap_next(size_t value, size_t count)
{
    return count == 0 ? 0 : (value + 1) % count;
}

void pdkpass_state_init(pdkpass_state_t *state)
{
    state->page = PDKPASS_PAGE_HOME;
    state->detail_origin = PDKPASS_PAGE_HOME;
    state->selected_race = 0;
    state->selected_driver = 0;
    state->selected_session = PDKPASS_SESSION_FP1;
    state->home_race = 0;
    state->home_browsing = false;
    state->season_complete = false;
    state->network_selection = 0;
}

void pdkpass_state_set_home_race(pdkpass_state_t *state, size_t race_index,
                                 size_t race_count)
{
    state->season_complete = race_index >= race_count;
    state->home_race = state->season_complete ? 0 : race_index;
    if (state->page == PDKPASS_PAGE_HOME && !state->home_browsing) {
        state->selected_race = state->home_race;
    }
}

void pdkpass_state_reset_home_race(pdkpass_state_t *state, size_t race_count)
{
    state->home_browsing = false;
    state->selected_race = state->season_complete
                               ? (race_count > 0 ? race_count - 1 : 0)
                               : state->home_race;
}

// Pure navigation state machine. UI rendering and hardware access intentionally
// stay out of this function so every button path can be covered by host tests.
void pdkpass_state_handle(pdkpass_state_t *state, pdkpass_input_t input,
                          size_t race_count, size_t driver_count)
{
    pdkpass_page_t previous_page = state->page;
    switch (state->page) {
    case PDKPASS_PAGE_HOME:
        if (input == PDKPASS_INPUT_BACK) state->page = PDKPASS_PAGE_NETWORK;
        if (input == PDKPASS_INPUT_UP_LONG) state->page = PDKPASS_PAGE_STANDINGS;
        if (input == PDKPASS_INPUT_DOWN_LONG) {
            if (race_count > 0) {
                state->selected_race = state->home_browsing
                                           ? state->selected_race
                                           : (state->season_complete ? race_count - 1
                                                                     : state->home_race);
            }
            state->page = PDKPASS_PAGE_CALENDAR;
        }
        if ((input == PDKPASS_INPUT_UP || input == PDKPASS_INPUT_DOWN) &&
            race_count > 0) {
            size_t current = state->season_complete && !state->home_browsing
                                 ? race_count - 1 : state->selected_race;
            state->selected_race = input == PDKPASS_INPUT_UP
                                       ? wrap_previous(current, race_count)
                                       : wrap_next(current, race_count);
            state->home_browsing = true;
        }
        if (input == PDKPASS_INPUT_OK && race_count > 0 &&
            (!state->season_complete || state->home_browsing)) {
            state->detail_origin = PDKPASS_PAGE_HOME;
            state->page = PDKPASS_PAGE_RACE_DETAIL;
        }
        break;

    case PDKPASS_PAGE_NETWORK:
        if (input == PDKPASS_INPUT_UP)
            state->network_selection = (state->network_selection + 2U) % 3U;
        if (input == PDKPASS_INPUT_DOWN)
            state->network_selection = (state->network_selection + 1U) % 3U;
        if (input == PDKPASS_INPUT_BACK ||
            (input == PDKPASS_INPUT_OK && state->network_selection == 2U))
            state->page = PDKPASS_PAGE_HOME;
        break;
    case PDKPASS_PAGE_NETWORK_CONFIRM:
    case PDKPASS_PAGE_NETWORK_PROGRESS:
        if (input == PDKPASS_INPUT_BACK) state->page = PDKPASS_PAGE_NETWORK;
        break;
    case PDKPASS_PAGE_CALENDAR:
        if (input == PDKPASS_INPUT_UP) {
            state->selected_race = wrap_previous(state->selected_race, race_count);
        } else if (input == PDKPASS_INPUT_DOWN) {
            state->selected_race = wrap_next(state->selected_race, race_count);
        } else if (input == PDKPASS_INPUT_OK && race_count > 0) {
            state->detail_origin = PDKPASS_PAGE_CALENDAR;
            state->page = PDKPASS_PAGE_RACE_DETAIL;
        } else if (input == PDKPASS_INPUT_BACK) {
            state->page = PDKPASS_PAGE_HOME;
        }
        break;

    case PDKPASS_PAGE_STANDINGS:
        if (input == PDKPASS_INPUT_UP) {
            state->selected_driver = wrap_previous(state->selected_driver, driver_count);
        } else if (input == PDKPASS_INPUT_DOWN) {
            state->selected_driver = wrap_next(state->selected_driver, driver_count);
        } else if (input == PDKPASS_INPUT_BACK) {
            state->page = PDKPASS_PAGE_HOME;
        }
        break;

    case PDKPASS_PAGE_RACE_DETAIL:
        if (input == PDKPASS_INPUT_UP) {
            state->selected_race = wrap_previous(state->selected_race, race_count);
        } else if (input == PDKPASS_INPUT_DOWN) {
            state->selected_race = wrap_next(state->selected_race, race_count);
        } else if (input == PDKPASS_INPUT_OK) {
            state->selected_session = PDKPASS_SESSION_FP1;
            state->page = PDKPASS_PAGE_RESULTS;
        } else if (input == PDKPASS_INPUT_BACK) {
            state->page = state->detail_origin;
        }
        break;

    case PDKPASS_PAGE_RESULTS:
        if (input == PDKPASS_INPUT_UP) {
            state->selected_session = (pdkpass_session_kind_t)wrap_previous(
                state->selected_session, PDKPASS_SESSION_COUNT);
        } else if (input == PDKPASS_INPUT_DOWN) {
            state->selected_session = (pdkpass_session_kind_t)wrap_next(
                state->selected_session, PDKPASS_SESSION_COUNT);
        } else if (input == PDKPASS_INPUT_OK || input == PDKPASS_INPUT_BACK) {
            state->page = PDKPASS_PAGE_RACE_DETAIL;
        }
        break;
    }
    if (previous_page != PDKPASS_PAGE_HOME && state->page == PDKPASS_PAGE_HOME &&
        !state->home_browsing) {
        pdkpass_state_reset_home_race(state, race_count);
    }
}
