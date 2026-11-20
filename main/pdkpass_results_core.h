#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PDKPASS_RESULT_DELAY_SECONDS 1800LL

typedef enum {
    PDKPASS_SESSION_FP1 = 0,
    PDKPASS_SESSION_FP2,
    PDKPASS_SESSION_FP3,
    PDKPASS_SESSION_SPRINT_QUALIFYING,
    PDKPASS_SESSION_SPRINT,
    PDKPASS_SESSION_QUALIFYING,
    PDKPASS_SESSION_RACE,
    PDKPASS_SESSION_COUNT,
} pdkpass_session_kind_t;

static inline uint32_t pdkpass_round_session_mask(const char *middle, uint32_t observed)
{
    const uint32_t sprint = (1U << PDKPASS_SESSION_SPRINT_QUALIFYING) |
                            (1U << PDKPASS_SESSION_SPRINT);
    const uint32_t practice = (1U << PDKPASS_SESSION_FP2) | (1U << PDKPASS_SESSION_FP3);
    uint32_t mask = observed | (1U << PDKPASS_SESSION_FP1) |
                    (1U << PDKPASS_SESSION_QUALIFYING) | (1U << PDKPASS_SESSION_RACE);
    if ((observed & sprint) || (!(observed & practice) && strncmp(middle, "SPR Q", 5U) == 0))
        mask |= sprint;
    else if ((observed & practice) || strncmp(middle, "QUALI", 5U) == 0)
        mask |= practice;
    return mask;
}

const char *pdkpass_session_label(pdkpass_session_kind_t kind);

// Returns PDKPASS_SESSION_COUNT for sessions that do not produce a race-weekend
// classification supported by PDKPASS (for example, pre-season testing).
pdkpass_session_kind_t pdkpass_session_kind_from_name(const char *name);

// Parse an OpenF1 ISO-8601 timestamp, including Z or a numeric UTC offset.
bool pdkpass_parse_iso8601_utc(const char *text, int64_t *epoch_utc);

bool pdkpass_session_result_due(int64_t now_utc, int64_t session_end_utc);

bool pdkpass_result_cache_identity_matches(unsigned cached_year,
                                           size_t cached_race_count,
                                           unsigned season_year,
                                           size_t season_race_count);
