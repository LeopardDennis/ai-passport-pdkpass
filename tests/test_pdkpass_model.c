#include <assert.h>
#include "pdkpass_model.h"

static void test_session_filter(void)
{
    pdkpass_state_t state;
    pdkpass_state_init(&state);
    state.page = PDKPASS_PAGE_RESULTS;
    // Exhaust all seven-session subsets, including empty and single-session
    // weekends. Both directions skip hidden entries and wrap within the set.
    for (uint32_t mask = 0; mask < (1U << PDKPASS_SESSION_COUNT); mask++) {
        state.selected_session = PDKPASS_SESSION_FP1;
        pdkpass_state_set_sessions(&state, mask);
        if (!mask) {
            assert(state.selected_session == PDKPASS_SESSION_COUNT);
            pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 2, 0, 11);
            pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 2, 0, 11);
            assert(state.selected_session == PDKPASS_SESSION_COUNT);
            continue;
        }
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) {
            if (!(mask & (1U << i))) continue;
            state.selected_session = (pdkpass_session_kind_t)i;
            pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 2, 0, 11);
            unsigned next = (i + 1) % PDKPASS_SESSION_COUNT;
            while (!(mask & (1U << next))) next = (next + 1) % PDKPASS_SESSION_COUNT;
            assert((unsigned)state.selected_session == next);
            pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 2, 0, 11);
            assert((unsigned)state.selected_session == i);
        }
    }
    // Removing a selected session repairs selection immediately; mask bits
    // outside the session enum cannot produce invalid shifts/navigation.
    state.selected_session = PDKPASS_SESSION_FP3;
    pdkpass_state_set_sessions(&state, (1U << PDKPASS_SESSION_SPRINT) | (1U << 31));
    assert(state.selected_session == PDKPASS_SESSION_SPRINT);
    assert(state.session_mask == (1U << PDKPASS_SESSION_SPRINT));
    pdkpass_state_set_sessions(&state, 0);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 2, 0, 11);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
}

int main(void)
{
    test_session_filter();
    pdkpass_state_t state;
    pdkpass_state_init(&state);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 0);
    assert(state.home_race == 0);
    assert(!state.home_browsing);
    assert(state.selected_session == PDKPASS_SESSION_FP1);
    assert(!state.season_complete);

    pdkpass_state_set_home_race(&state, 3, 11);
    assert(state.home_race == 3);
    assert(state.selected_race == 3);

    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 2);
    assert(state.home_browsing);
    pdkpass_state_set_home_race(&state, 4, 11);
    assert(state.home_race == 4);
    assert(state.selected_race == 2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 3);
    pdkpass_state_reset_home_race(&state, 11);
    assert(!state.home_browsing);
    assert(state.selected_race == 4);

    pdkpass_state_handle(&state, PDKPASS_INPUT_UP_LONG, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_STANDINGS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_driver == 22);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);

    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_TEAM_STANDINGS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_team == 10);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_team == 0 && state.selected_driver == 22);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_TEAM_STANDINGS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 0);
    assert(state.selected_team == 0);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME && state.selected_race == 4);
    // The retained calendar state remains testable, with no home shortcut.
    state.page = PDKPASS_PAGE_CALENDAR;
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_race == 5);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_race == 4);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_race == 3);

    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_race == 4);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RESULTS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_session == PDKPASS_SESSION_RACE);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_session == PDKPASS_SESSION_FP1);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RESULTS);
    assert(state.selected_session == PDKPASS_SESSION_FP1);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_CALENDAR);

    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == state.home_race);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_TEAM_STANDINGS);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    pdkpass_state_reset_home_race(&state, 11);
    pdkpass_state_set_home_race(&state, 11, 11);
    assert(state.season_complete);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_TEAM_STANDINGS);
    assert(state.selected_race == 0);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 9);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23, 11);
    pdkpass_state_reset_home_race(&state, 11);
    assert(state.season_complete && !state.home_browsing);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 23, 23, 11);
    assert(state.page == PDKPASS_PAGE_NETWORK);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 23, 23, 11);
    assert(state.network_selection == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 23, 23, 11);
    assert(state.page == PDKPASS_PAGE_HOME);
    return 0;
}
