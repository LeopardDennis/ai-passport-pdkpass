#include <assert.h>
#include <string.h>
#include "pdkpass_data.h"
#include "pdkpass_schedule.h"

int main(void)
{
    assert(pdkpass_race_count == 23);
    for (size_t i = 0; i < pdkpass_race_count; i++) {
        assert(pdkpass_races[i].round == i + 1U);
        assert(pdkpass_races[i].circuit_length_m > 0);
        assert(pdkpass_races[i].laps > 0);
        assert(strstr(pdkpass_races[i].race_cn, "TBD") == NULL);
        if (i) assert(pdkpass_races[i - 1U].switch_at_utc <
                      pdkpass_races[i].switch_at_utc);
    }
    assert(strcmp(pdkpass_races[0].circuit, "MELBOURNE") == 0);
    assert(strcmp(pdkpass_races[11].circuit, "ZANDVOORT") == 0);
    assert(strcmp(pdkpass_races[4].race_cn, "RACE  25 MAY 04:00") == 0);
    pdkpass_race_t cached[PDKPASS_MAX_RACES];
    memcpy(cached, pdkpass_races + 12, 11 * sizeof(*cached));
    cached[0].meeting_key = 1293;
    cached[0].switch_at_utc += 60;
    assert(pdkpass_restore_legacy_calendar(2027, cached, 11, 24) == 11);
    assert(pdkpass_restore_legacy_calendar(2026, cached, 11, 11) == 11);
    assert(pdkpass_restore_legacy_calendar(2026, cached, 11, 24) == 23);
    assert(cached[12].meeting_key == 1293);
    assert(cached[12].switch_at_utc == pdkpass_races[12].switch_at_utc + 60);
    assert(memcmp(cached, pdkpass_races, 12 * sizeof(*cached)) == 0);
    assert(pdkpass_restore_legacy_calendar(2026, cached, 23, 24) == 23);
    assert(pdkpass_restore_legacy_calendar(2026, cached, 11, 24) == 11);
    assert(pdkpass_schedule_next_race(0, pdkpass_races,
                                      pdkpass_race_count) == 0);
    assert(pdkpass_schedule_next_race(1788713999, pdkpass_races,
                                      pdkpass_race_count) == 12);
    assert(pdkpass_schedule_next_race(1788714000, pdkpass_races,
                                      pdkpass_race_count) == 13);
    assert(pdkpass_schedule_next_race(1796576399, pdkpass_races,
                                      pdkpass_race_count) == 22);
    assert(pdkpass_schedule_next_race(1796576400, pdkpass_races,
                                      pdkpass_race_count) == pdkpass_race_count);
    assert(pdkpass_schedule_next_race(0, NULL, 0) == 0);

    // The Italy switch boundary is 01:00 Beijing time. Before midnight the
    // next check is midnight; at midnight it is the race boundary one hour on.
    assert(pdkpass_schedule_next_check(1788710399, pdkpass_races,
                                       pdkpass_race_count) == 1788710400);
    assert(pdkpass_schedule_next_check(1788710400, pdkpass_races,
                                       pdkpass_race_count) == 1788714000);
    assert(pdkpass_schedule_next_check(1788714000, pdkpass_races,
                                       pdkpass_race_count) == 1788796800);
    return 0;
}
