#include "pdkpass_season_core.h"

#include <stdio.h>

#define SECONDS_PER_DAY 86400LL

typedef struct {
    int year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
} beijing_parts_t;

static void civil_from_days(int64_t days, int *year, unsigned *month,
                            unsigned *day)
{
    days += 719468LL;
    int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    unsigned day_of_era = (unsigned)(days - era * 146097LL);
    unsigned year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) /
        365U;
    int value_year = (int)year_of_era + (int)era * 400;
    unsigned day_of_year =
        day_of_era - (365U * year_of_era + year_of_era / 4U -
                      year_of_era / 100U);
    unsigned month_prime = (5U * day_of_year + 2U) / 153U;
    unsigned value_day =
        day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    unsigned value_month = month_prime < 10U ? month_prime + 3U
                                              : month_prime - 9U;
    value_year += value_month <= 2U;
    *year = value_year;
    *month = value_month;
    *day = value_day;
}

static beijing_parts_t beijing_parts(int64_t epoch_utc)
{
    int64_t local = epoch_utc + PDKPASS_BEIJING_OFFSET_SECONDS;
    int64_t days = local / SECONDS_PER_DAY;
    int64_t seconds = local % SECONDS_PER_DAY;
    if (seconds < 0) {
        seconds += SECONDS_PER_DAY;
        days--;
    }
    beijing_parts_t parts = {0};
    civil_from_days(days, &parts.year, &parts.month, &parts.day);
    parts.hour = (unsigned)(seconds / 3600LL);
    parts.minute = (unsigned)((seconds % 3600LL) / 60LL);
    return parts;
}

static const char *month_label(unsigned month)
{
    static const char *labels[] = {
        "---", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    return month <= 12U ? labels[month] : labels[0];
}

unsigned pdkpass_beijing_year(int64_t epoch_utc)
{
    beijing_parts_t parts = beijing_parts(epoch_utc);
    return parts.year > 0 ? (unsigned)parts.year : 0U;
}

int64_t pdkpass_next_beijing_midnight(int64_t epoch_utc)
{
    int64_t local = epoch_utc + PDKPASS_BEIJING_OFFSET_SECONDS;
    int64_t day = local / SECONDS_PER_DAY;
    if (local < 0 && local % SECONDS_PER_DAY) day--;
    return (day + 1LL) * SECONDS_PER_DAY -
           PDKPASS_BEIJING_OFFSET_SECONDS;
}

bool pdkpass_season_candidate_valid(unsigned current_year,
                                    unsigned candidate_year,
                                    size_t candidate_race_count)
{
    return candidate_year >= 2026U && candidate_year >= current_year &&
           candidate_race_count > 0U &&
           candidate_race_count <= 24U;
}

void pdkpass_format_beijing_weekend(int64_t start_utc, int64_t end_utc,
                                    char *output, size_t capacity)
{
    if (!output || capacity == 0U) return;
    beijing_parts_t start = beijing_parts(start_utc);
    beijing_parts_t end = beijing_parts(end_utc);
    if (start.month == end.month) {
        snprintf(output, capacity, "%02u-%02u %s", start.day, end.day,
                 month_label(end.month));
    } else {
        snprintf(output, capacity, "%02u %s-%02u %s", start.day,
                 month_label(start.month), end.day, month_label(end.month));
    }
}

void pdkpass_format_beijing_session(const char *label, int64_t start_utc,
                                    char *output, size_t capacity)
{
    if (!output || capacity == 0U) return;
    beijing_parts_t parts = beijing_parts(start_utc);
    snprintf(output, capacity, "%-5s %02u %s %02u:%02u",
             label ? label : "EVENT", parts.day, month_label(parts.month),
             parts.hour, parts.minute);
}

void pdkpass_format_beijing_date(int64_t epoch_utc, char *output,
                                 size_t capacity)
{
    if (!output || capacity == 0U) return;
    beijing_parts_t parts = beijing_parts(epoch_utc);
    snprintf(output, capacity, "%02u %s", parts.day,
             month_label(parts.month));
}

int64_t pdkpass_season_next_race_check(int64_t now_utc, int64_t race_end_utc)
{
    int64_t deadline = pdkpass_next_beijing_midnight(now_utc);
    if (race_end_utc > now_utc && race_end_utc < deadline) deadline = race_end_utc;
    int64_t standings_at = race_end_utc + 1800LL;
    if (standings_at > now_utc && standings_at < deadline) deadline = standings_at;
    return deadline;
}
