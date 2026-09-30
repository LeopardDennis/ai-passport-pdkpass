#pragma once
#include "pdkpass_data.h"
#include "pdkpass_results_core.h"
#include <stdbool.h>
#include <stdint.h>

#define PDKPASS_REMINDER_LEAD_SECONDS 600LL
#define PDKPASS_REMINDER_CAPACITY (PDKPASS_MAX_RACES * PDKPASS_SESSION_COUNT)
#define PDKPASS_REMINDER_CANCELLED 1U
#define PDKPASS_REMINDER_FIRED 2U

typedef struct {
    int64_t start_utc;
    int32_t session_key;
    uint8_t round, kind, flags, reserved;
} pdkpass_reminder_entry_t;

typedef struct {
    uint16_t year;
    uint8_t enabled, reserved;
    int32_t meeting_keys[PDKPASS_MAX_RACES];
    pdkpass_reminder_entry_t entries[PDKPASS_REMINDER_CAPACITY];
} pdkpass_reminder_schedule_t;

// Merge only a complete round response; absent sessions are cancelled. Keep
// delivered identities across schedule changes and reset only for a newer year.
bool pdkpass_reminder_merge(pdkpass_reminder_schedule_t *schedule, unsigned year,
    unsigned round, int32_t meeting_key,
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT]);
int pdkpass_reminder_due(const pdkpass_reminder_schedule_t *schedule,
                         bool time_valid, int64_t now);
uint32_t pdkpass_reminder_wait(const pdkpass_reminder_schedule_t *schedule,
                              bool time_valid, int64_t now);

// Earliest strictly future, non-cancelled session; independent of alert settings.
int pdkpass_reminder_next_session(
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT], int64_t now);
