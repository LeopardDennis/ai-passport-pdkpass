#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PDKPASS_BEIJING_OFFSET_SECONDS 28800LL

unsigned pdkpass_beijing_year(int64_t epoch_utc);
int64_t pdkpass_next_beijing_midnight(int64_t epoch_utc);

// A new cache is adopted only when it is not older than the current cache and
// contains at least one confirmed Grand Prix meeting.
bool pdkpass_season_candidate_valid(unsigned current_year,
                                    unsigned candidate_year,
                                    size_t candidate_race_count);

void pdkpass_format_beijing_weekend(int64_t start_utc, int64_t end_utc,
                                    char *output, size_t capacity);
void pdkpass_format_beijing_session(const char *label, int64_t start_utc,
                                    char *output, size_t capacity);
void pdkpass_format_beijing_date(int64_t epoch_utc, char *output,
                                 size_t capacity);

// Next switch or delayed standings boundary, capped at Beijing midnight.
int64_t pdkpass_season_next_race_check(int64_t now_utc, int64_t race_end_utc);
