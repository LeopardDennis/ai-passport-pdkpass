#include <assert.h>
#include <string.h>

#include "pdkpass_season_core.h"

int main(void)
{
    int64_t race_end = 1788688800LL; // 2026-09-06 18:00 CST, synthetic boundary
    assert(pdkpass_season_next_race_check(race_end - 1, race_end) == race_end);
    assert(pdkpass_season_next_race_check(race_end, race_end) == race_end + 1800);
    assert(pdkpass_season_next_race_check(race_end + 1799, race_end) == race_end + 1800);
    assert(pdkpass_season_next_race_check(race_end + 1800, race_end) ==
           pdkpass_next_beijing_midnight(race_end));

    // Beijing crosses into 2027 at 2026-12-31 16:00:00 UTC.
    assert(pdkpass_beijing_year(1798732799) == 2026);
    assert(pdkpass_beijing_year(1798732800) == 2027);
    assert(pdkpass_next_beijing_midnight(1798732799) == 1798732800);
    assert(pdkpass_next_beijing_midnight(1798732800) == 1798819200);

    assert(pdkpass_season_candidate_valid(2026, 2026, 23));
    assert(pdkpass_season_candidate_valid(2026, 2027, 24));
    assert(!pdkpass_season_candidate_valid(2026, 2027, 0));
    assert(!pdkpass_season_candidate_valid(2027, 2026, 23));
    assert(!pdkpass_season_candidate_valid(2027, 2027, 25));

    char text[32];
    pdkpass_format_beijing_weekend(1788517800, 1788706800, text,
                                   sizeof(text));
    assert(strcmp(text, "04-06 SEP") == 0);
    pdkpass_format_beijing_session("FP1", 1788517800, text, sizeof(text));
    assert(strcmp(text, "FP1   04 SEP 18:30") == 0);
    pdkpass_format_beijing_date(1788517800, text, sizeof(text));
    assert(strcmp(text, "04 SEP") == 0);
    const char *weekends[][2] = {
        {"04-06 SEP", "SEP 04-06"},
        {"31 OCT-02 NOV", "OCT 31-NOV 02"},
        {"30 OCT-01 NOV", "OCT 30-NOV 01"},
        {"30 APR-02 MAY", "APR 30-MAY 02"},
        {"30 JUL-01 AUG", "JUL 30-AUG 01"},
        {"31 DEC-02 JAN", "DEC 31-JAN 02"},
        {"TIME TBD", "TIME TBD"},
        {"", ""},
    };
    for (size_t i = 0; i < sizeof(weekends) / sizeof(weekends[0]); i++) {
        pdkpass_format_home_weekend(weekends[i][0], text, sizeof(text));
        assert(strcmp(text, weekends[i][1]) == 0);
    }
    char small[4];
    pdkpass_format_home_weekend("31 OCT-02 NOV", small, sizeof(small));
    assert(strcmp(small, "OCT") == 0);
    pdkpass_format_home_weekend(NULL, text, sizeof(text));
    assert(text[0] == '\0');
    pdkpass_format_home_weekend("31 OCT-02 NOV", NULL, 0);
    return 0;
}
