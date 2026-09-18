#include "pdkpass_calendar.h"
#include "pdkpass_tracks.h"
#include <stdio.h>
#include <string.h>

// F1/FIA announcement, 2026-09-16:
// https://corp.formula1.com/2027-calendar-announced-with-10-sprint-events/
// Dates are venue-local, NOT session start/end times. Boundary is midnight
// after the last published date in the venue's IANA zone (2026 tzdata rules).
// Istanbul remains subject to FIA homologation. No lap counts are assumed.
typedef struct {
    const char *track_id, *country, *api_country, *weekend;
    int64_t weekend_end_utc;
    bool sprint;
} calendar_entry_t;
static const calendar_entry_t s_2027[] = {
    { "sakhir", "BAHRAIN", "Bahrain", "12-14 MAR", 1805058000LL, true }, // 03-12..03-14, Asia/Bahrain
    { "jeddah", "SAUDI ARABIA", "Saudi%20Arabia", "19-21 MAR", 1805662800LL, false }, // 03-19..03-21, Asia/Riyadh
    { "melbourne", "AUSTRALIA", "Australia", "02-04 APR", 1806847200LL, true }, // 04-02..04-04, Australia/Melbourne
    { "suzuka", "JAPAN", "Japan", "09-11 APR", 1807455600LL, true }, // 04-09..04-11, Asia/Tokyo
    { "shanghai", "CHINA", "China", "16-18 APR", 1808064000LL, false }, // 04-16..04-18, Asia/Shanghai
    { "miami", "USA", "United%20States", "30 APR-02 MAY", 1809316800LL, false }, // 04-30..05-02, America/New_York
    { "montreal", "CANADA", "Canada", "21-23 MAY", 1811131200LL, true }, // 05-21..05-23, America/Toronto
    { "monaco", "MONACO", "Monaco", "04-06 JUN", 1812319200LL, true }, // 06-04..06-06, Europe/Monaco
    { "portimao", "PORTUGAL", "Portugal", "18-20 JUN", 1813532400LL, false }, // 06-18..06-20, Europe/Lisbon
    { "silverstone", "BRITAIN", "United%20Kingdom", "02-04 JUL", 1814742000LL, true }, // 07-02..07-04, Europe/London
    { "spielberg", "AUSTRIA", "Austria", "09-11 JUL", 1815343200LL, false }, // 07-09..07-11, Europe/Vienna
    { "spa", "BELGIUM", "Belgium", "23-25 JUL", 1816552800LL, false }, // 07-23..07-25, Europe/Brussels
    { "hungaroring", "HUNGARY", "Hungary", "30 JUL-01 AUG", 1817157600LL, false }, // 07-30..08-01, Europe/Budapest
    { "monza", "ITALY", "Italy", "03-05 SEP", 1820181600LL, true }, // 09-03..09-05, Europe/Rome
    { "madring", "SPAIN", "Spain", "10-12 SEP", 1820786400LL, false }, // 09-10..09-12, Europe/Madrid
    { "baku", "AZERBAIJAN", "Azerbaijan", "24-26 SEP", 1821988800LL, false }, // 09-24..09-26, Asia/Baku
    { "istanbul", "TURKEY", "Turkey", "01-03 OCT", 1822597200LL, false }, // 10-01..10-03, Europe/Istanbul
    { "singapore", "SINGAPORE", "Singapore", "08-10 OCT", 1823184000LL, false }, // 10-08..10-10, Asia/Singapore
    { "austin", "USA", "United%20States", "22-24 OCT", 1824440400LL, false }, // 10-22..10-24, America/Chicago
    { "mexico_city", "MEXICO", "Mexico", "29-31 OCT", 1825048800LL, false }, // 10-29..10-31, America/Mexico_City
    { "interlagos", "BRAZIL", "Brazil", "05-07 NOV", 1825642800LL, true }, // 11-05..11-07, America/Sao_Paulo
    { "las_vegas", "LAS VEGAS", "United%20States", "18-20 NOV", 1826784000LL, false }, // 11-18..11-20, America/Los_Angeles
    { "losail", "QATAR", "Qatar", "03-05 DEC", 1828040400LL, true }, // 12-03..12-05, Asia/Qatar
    { "yas_marina", "ABU DHABI", "United%20Arab%20Emirates", "10-12 DEC", 1828641600LL, true }, // 12-10..12-12, Asia/Dubai
};

bool pdkpass_calendar_supported(unsigned year) { return year == 2026 || year == 2027; }

bool pdkpass_calendar_load(unsigned year, pdkpass_season_snapshot_t *out)
{
    if (!out || !pdkpass_calendar_supported(year)) return false;
    memset(out, 0, sizeof(*out));
    out->year = (uint16_t)year;
    if (year == 2026) {
        out->race_count = (uint8_t)pdkpass_race_count;
        out->driver_count = (uint8_t)pdkpass_driver_count;
        memcpy(out->races, pdkpass_races, pdkpass_race_count * sizeof(out->races[0]));
        memcpy(out->drivers, pdkpass_drivers, pdkpass_driver_count * sizeof(out->drivers[0]));
        snprintf(out->standings_as_of, sizeof(out->standings_as_of), "31 AUG");
    } else {
        out->race_count = sizeof(s_2027) / sizeof(s_2027[0]);
        snprintf(out->standings_as_of, sizeof(out->standings_as_of), "PENDING");
        for (size_t i = 0; i < out->race_count; i++) {
            const calendar_entry_t *entry = &s_2027[i];
            const pdkpass_track_info_t *track = pdkpass_track_find(entry->track_id);
            pdkpass_race_t *race = &out->races[i];
            race->round = (uint8_t)(i + 1);
            race->switch_at_utc = entry->weekend_end_utc;
            snprintf(race->circuit, sizeof(race->circuit), "%s", track->name);
            snprintf(race->country, sizeof(race->country), "%s", entry->country);
            snprintf(race->api_country, sizeof(race->api_country), "%s", entry->api_country);
            snprintf(race->weekend, sizeof(race->weekend), "%s", entry->weekend);
            snprintf(race->session_one_cn, sizeof(race->session_one_cn), "FP1 TIME TBD");
            snprintf(race->session_two_cn, sizeof(race->session_two_cn),
                     "%s TIME TBD", entry->sprint ? "SPR Q" : "QUALI");
            snprintf(race->race_cn, sizeof(race->race_cn), "RACE TIME TBD");
        }
    }
    for (size_t i = 0; i < out->race_count; i++) {
        const pdkpass_track_info_t *track = pdkpass_track_find(out->races[i].circuit);
        if (track) {
            out->races[i].circuit_length_m = track->length_m;
            out->races[i].accent = track->accent;
        }
    }
    return true;
}
