#include <assert.h>
#include "pdkpass_model.h"

static void test_detail_selection(void)
{
    const uint32_t normal = (1U << PDKPASS_SESSION_FP1) | (1U << PDKPASS_SESSION_FP2) |
        (1U << PDKPASS_SESSION_FP3) | (1U << PDKPASS_SESSION_QUALIFYING) |
        (1U << PDKPASS_SESSION_RACE);
    pdkpass_detail_session_t sessions[PDKPASS_SESSION_COUNT] = {0};
    for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        sessions[i].start_utc = 100 + i * 100;
        sessions[i].end_utc = 150 + i * 100;
    }
    pdkpass_state_t state;
    pdkpass_state_init(&state);
    state.page = PDKPASS_PAGE_RACE_DETAIL;
    state.selected_race = 4;
    // No result is required: FP2 just ended, while FP3 is still in progress.
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 325);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 24, 0, 0);
    assert(state.page == PDKPASS_PAGE_RESULTS && state.selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_set_sessions(&state, normal);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 24, 0, 0);
    assert(state.selected_session == PDKPASS_SESSION_FP3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 24, 0, 0);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    // Live completions never steal a browsed selection. Reopening chooses anew.
    pdkpass_state_set_detail_sessions(&state, normal, sessions, false, true, 900);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 900);
    assert(state.detail_selected_session == PDKPASS_SESSION_RACE);
    assert(pdkpass_state_detail_index(&state) / PDKPASS_DETAIL_ROWS == 1);
    sessions[PDKPASS_SESSION_RACE].cancelled = true;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 900);
    assert(state.detail_selected_session == PDKPASS_SESSION_QUALIFYING);
    // Prefer the most recent actual end, not enum order or the last ready result.
    sessions[PDKPASS_SESSION_FP2].end_utc = 800;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 900);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 50);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP1);
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, false, 900);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP1);
    // A start in the past alone does not prove completion.
    for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) sessions[i].end_utc = 0;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, true, 225);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP3);
    // Offline caches still identify completed sessions without a valid clock.
    sessions[PDKPASS_SESSION_FP2].result_ready = true;
    sessions[PDKPASS_SESSION_QUALIFYING].result_ready = true;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, false, 0);
    assert(state.detail_selected_session == PDKPASS_SESSION_QUALIFYING);
    // Published changes to start order determine navigation order too.
    sessions[PDKPASS_SESSION_FP2].start_utc = 625;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, true, false, 0);
    assert(state.detail_sessions[1] == PDKPASS_SESSION_FP3);
    assert(state.detail_sessions[2] == PDKPASS_SESSION_QUALIFYING);
    assert(state.detail_sessions[3] == PDKPASS_SESSION_FP2);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    sessions[PDKPASS_SESSION_FP3].start_utc = 0;
    pdkpass_state_set_detail_sessions(&state, normal, sessions, false, false, 0);
    assert(state.detail_sessions[2] == PDKPASS_SESSION_FP3); // unknown keeps its slot
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
}

static void test_detail_paging(void)
{
    pdkpass_detail_session_t sessions[PDKPASS_SESSION_COUNT] = {0};
    // Exercise normal/sprint, empty, cancelled/removed and every other subset.
    for (uint32_t mask = 0; mask < (1U << PDKPASS_SESSION_COUNT); mask++) {
        pdkpass_state_t state;
        pdkpass_state_init(&state);
        state.page = PDKPASS_PAGE_RACE_DETAIL;
        state.selected_race = 8;
        pdkpass_state_set_detail_sessions(&state, mask, sessions, true, false, 0);
        if (!mask) {
            pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 24, 0, 0);
            assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
        }
        for (size_t i = 0; i < state.detail_count; i++) {
            assert(pdkpass_state_detail_index(&state) == i);
            assert(state.detail_selected_session == state.detail_sessions[i]);
            assert((mask & (1U << state.detail_selected_session)) != 0);
            pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 24, 0, 0);
        }
        if (state.detail_count) {
            assert(pdkpass_state_detail_index(&state) == state.detail_count - 1);
            for (size_t i = state.detail_count; i > 1; i--)
                pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 24, 0, 0);
            pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 24, 0, 0);
            assert(pdkpass_state_detail_index(&state) == 0);
            // A removed selection is repaired and never opens the absent result.
            uint32_t next = mask & ~(1U << state.detail_selected_session);
            pdkpass_state_set_detail_sessions(&state, next, sessions, false, false, 0);
            assert(state.detail_count == 0 || (next & (1U << state.detail_selected_session)));
        }
        assert(state.selected_race == 8);
    }
}

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
    test_detail_selection();
    test_detail_paging();
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
    pdkpass_detail_session_t detail[PDKPASS_SESSION_COUNT] = {0};
    pdkpass_state_set_detail_sessions(&state, (1U << PDKPASS_SESSION_COUNT) - 1U,
                                      detail, true, false, 0);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_race == 3);
    assert(state.detail_selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RESULTS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23, 11);
    assert(state.selected_session == PDKPASS_SESSION_FP1);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23, 11);
    assert(state.selected_session == PDKPASS_SESSION_FP2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23, 11);
    assert(state.page == PDKPASS_PAGE_RESULTS);
    assert(state.selected_session == PDKPASS_SESSION_FP2);
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
