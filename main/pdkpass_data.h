#pragma once

#include <stddef.h>
#include <stdint.h>

#define PDKPASS_MAX_RACES 24
#define PDKPASS_MAX_DRIVERS 24
#define PDKPASS_COUNTRY_LEN 22
#define PDKPASS_CIRCUIT_LEN 22
#define PDKPASS_WEEKEND_LEN 18
#define PDKPASS_SESSION_LINE_LEN 24
#define PDKPASS_API_COUNTRY_LEN 32
#define PDKPASS_DRIVER_NAME_LEN 16
#define PDKPASS_TEAM_LEN 18

typedef struct {
    int32_t meeting_key;
    int64_t switch_at_utc;
    uint32_t accent;
    uint16_t circuit_length_m;
    uint8_t round;
    uint8_t laps;
    char country[PDKPASS_COUNTRY_LEN];
    char circuit[PDKPASS_CIRCUIT_LEN];
    char weekend[PDKPASS_WEEKEND_LEN];
    char session_one_cn[PDKPASS_SESSION_LINE_LEN];
    char session_two_cn[PDKPASS_SESSION_LINE_LEN];
    char race_cn[PDKPASS_SESSION_LINE_LEN];
    // Percent-encoded OpenF1 country_name used to discover session metadata.
    char api_country[PDKPASS_API_COUNTRY_LEN];
} pdkpass_race_t;

typedef struct {
    uint32_t accent;
    uint16_t points_tenths;
    uint8_t position;
    uint8_t driver_number;
    char code[4];
    char name[PDKPASS_DRIVER_NAME_LEN];
    char team[PDKPASS_TEAM_LEN];
} pdkpass_driver_t;

extern const pdkpass_race_t pdkpass_races[];
extern const size_t pdkpass_race_count;
// Repair only the legacy 2026 R13-R23 cache, preserving its updated race fields.
// Returns the resulting count; other seasons/calendar shapes remain untouched.
size_t pdkpass_restore_legacy_calendar(unsigned year, pdkpass_race_t *races,
                                       size_t count, size_t capacity);
extern const pdkpass_driver_t pdkpass_drivers[];
extern const size_t pdkpass_driver_count;
