#include <assert.h>
#include "pdkpass_model.h"

int main(void)
{
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

    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 2);
    assert(state.home_browsing);
    pdkpass_state_set_home_race(&state, 4, 11);
    assert(state.home_race == 4);
    assert(state.selected_race == 2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 3);
    pdkpass_state_reset_home_race(&state, 11);
    assert(!state.home_browsing);
    assert(state.selected_race == 4);

    pdkpass_state_handle(&state, PDKPASS_INPUT_UP_LONG, 11, 23);
    assert(state.page == PDKPASS_PAGE_STANDINGS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.selected_driver == 22);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);

    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23);
    assert(state.page == PDKPASS_PAGE_CALENDAR);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23);
    assert(state.selected_race == 5);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.selected_race == 4);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.selected_race == 3);

    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23);
    assert(state.selected_race == 4);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_RESULTS);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.selected_session == PDKPASS_SESSION_RACE);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN, 11, 23);
    assert(state.selected_session == PDKPASS_SESSION_FP1);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    assert(state.page == PDKPASS_PAGE_CALENDAR);

    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == state.home_race);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23);
    assert(state.page == PDKPASS_PAGE_CALENDAR);
    assert(state.selected_race == 3);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    pdkpass_state_reset_home_race(&state, 11);
    pdkpass_state_set_home_race(&state, 11, 11);
    assert(state.season_complete);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    pdkpass_state_handle(&state, PDKPASS_INPUT_DOWN_LONG, 11, 23);
    assert(state.page == PDKPASS_PAGE_CALENDAR);
    assert(state.selected_race == 10);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 23, 23);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 11, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    assert(state.selected_race == 9);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 11, 23);
    assert(state.page == PDKPASS_PAGE_RACE_DETAIL);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 11, 23);
    pdkpass_state_reset_home_race(&state, 11);
    assert(state.season_complete && !state.home_browsing);
    pdkpass_state_handle(&state, PDKPASS_INPUT_BACK, 23, 23);
    assert(state.page == PDKPASS_PAGE_NETWORK);
    pdkpass_state_handle(&state, PDKPASS_INPUT_UP, 23, 23);
    assert(state.network_selection == 2);
    pdkpass_state_handle(&state, PDKPASS_INPUT_OK, 23, 23);
    assert(state.page == PDKPASS_PAGE_HOME);
    return 0;
}
