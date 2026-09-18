#include "pdkpass_calendar.h"
#include "pdkpass_tracks.h"
#include "pdkpass_schedule.h"
#include "pdkpass_season_core.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    pdkpass_season_snapshot_t season;
    assert(pdkpass_calendar_load(2026, &season));
    assert(season.race_count == 23 && season.driver_count > 0);
    assert(pdkpass_calendar_load(2027, &season));
    assert(season.year == 2027 && season.race_count == 24 && season.driver_count == 0);
    assert(strcmp(season.standings_as_of, "PENDING") == 0);
    unsigned sprints = 0;
    for (size_t i = 0; i < season.race_count; i++) {
        pdkpass_race_t *race = &season.races[i];
        const pdkpass_track_info_t *track = pdkpass_track_find(race->circuit);
        assert(track && track->length_m == race->circuit_length_m);
        assert(track->accent == race->accent);
        assert(race->meeting_key == 0 && race->laps == 0 && race->round == i+1);
        assert(strcmp(race->race_cn, "RACE TIME TBD") == 0);
        assert(strstr(race->session_one_cn, "TBD") && strstr(race->session_two_cn, "TBD"));
        sprints += strncmp(race->session_two_cn, "SPR Q", 5) == 0;
        if (i) assert(race->switch_at_utc > season.races[i-1].switch_at_utc);
        assert(pdkpass_schedule_next_race(race->switch_at_utc-1, season.races, 24) == i);
        assert(pdkpass_schedule_next_race(race->switch_at_utc, season.races, 24) == i+1);
    }
    assert(sprints == 10);
    assert(strcmp(season.races[0].circuit, "SAKHIR") == 0);
    assert(strcmp(season.races[8].circuit, "PORTIMAO") == 0);
    assert(strcmp(season.races[16].circuit, "ISTANBUL") == 0);
    assert(strcmp(season.races[23].circuit, "YAS MARINA") == 0);
    assert(strcmp(season.races[12].weekend, "30 JUL-01 AUG") == 0);
    // Local midnight after Vegas' Saturday weekend is Sunday 08:00 UTC.
    assert(season.races[21].switch_at_utc == 1826784000LL);
    assert(pdkpass_beijing_year(1798732799LL) == 2026);
    assert(pdkpass_beijing_year(1798732800LL) == 2027);
    assert(!pdkpass_calendar_load(2028, &season) && season.year == 2027);
    assert(!pdkpass_calendar_load(2027, NULL));
    return 0;
}
