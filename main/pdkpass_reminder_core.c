#include "pdkpass_reminder_core.h"
#include <string.h>

bool pdkpass_reminder_merge(pdkpass_reminder_schedule_t *s, unsigned year,
    unsigned round, int32_t meeting_key,
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT])
{
    if (!s || !entries || year < 2026 || year > 2100 || year < s->year ||
        round == 0 || round > PDKPASS_MAX_RACES || meeting_key <= 0) return false;
    bool new_year = year != s->year;
    size_t slot = PDKPASS_MAX_RACES;
    for (size_t i = 0; !new_year && i < PDKPASS_MAX_RACES; i++)
        if (s->meeting_keys[i] == meeting_key) { slot = i; break; }
    const pdkpass_reminder_entry_t *previous = slot < PDKPASS_MAX_RACES
        ? &s->entries[slot * PDKPASS_SESSION_COUNT] : NULL;
    // Validate the combined keys before mutating, including the year transition.
    int32_t keys[PDKPASS_SESSION_COUNT];
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        if (entries[i].session_key < 0 ||
            (entries[i].session_key && !(entries[i].flags & PDKPASS_REMINDER_CANCELLED) &&
             entries[i].start_utc < 1767225600LL))
            return false;
        keys[i] = !entries[i].session_key && previous ? previous[i].session_key : entries[i].session_key;
        for (size_t j = 0; j < i; j++)
            if (keys[i] && keys[i] == keys[j])
                return false;
    }
    bool changed = new_year;
    if (new_year) {
        memset(s->entries, 0, sizeof(s->entries));
        memset(s->meeting_keys, 0, sizeof(s->meeting_keys));
        s->year = (uint16_t)year;
    }
    if (slot == PDKPASS_MAX_RACES) {
        if (s->meeting_keys[round - 1U] == 0) slot = round - 1U;
        else for (size_t i = 0; i < PDKPASS_MAX_RACES; i++)
            if (s->meeting_keys[i] == 0) { slot = i; break; }
    }
    if (slot == PDKPASS_MAX_RACES) return false;
    if (s->meeting_keys[slot] != meeting_key) { s->meeting_keys[slot] = meeting_key; changed = true; }
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        pdkpass_reminder_entry_t *old = &s->entries[slot * PDKPASS_SESSION_COUNT + i];
        pdkpass_reminder_entry_t next = entries[i];
        if (!next.session_key) {
            // An omitted session can still be scheduled and eligible for an alert.
            if (old->session_key && old->round != round) {
                old->round = (uint8_t)round;
                changed = true;
            }
            continue;
        }
        next.round = (uint8_t)round;
        next.kind = (uint8_t)i;
        next.reserved = 0;
        next.flags &= PDKPASS_REMINDER_CANCELLED;
        for (size_t j = 0; j < PDKPASS_REMINDER_CAPACITY; j++)
            if (s->entries[j].session_key == next.session_key)
                next.flags |= s->entries[j].flags & PDKPASS_REMINDER_FIRED;
        if (memcmp(old, &next, sizeof(next))) { *old = next; changed = true; }
    }
    return changed;
}

static bool eligible(const pdkpass_reminder_entry_t *e, int64_t now)
{
    return e->session_key > 0 && e->round > 0 && e->round <= PDKPASS_MAX_RACES &&
           e->kind < PDKPASS_SESSION_COUNT && e->flags == 0 && e->start_utc > now;
}

int pdkpass_reminder_due(const pdkpass_reminder_schedule_t *s,
                         bool time_valid, int64_t now)
{
    if (!s || !s->enabled || !time_valid || now < 1767225600LL) return -1;
    int selected = -1;
    for (size_t i = 0; i < PDKPASS_REMINDER_CAPACITY; i++) {
        const pdkpass_reminder_entry_t *e = &s->entries[i];
        if (eligible(e, now) && now >= e->start_utc - PDKPASS_REMINDER_LEAD_SECONDS &&
            (selected < 0 || e->start_utc < s->entries[selected].start_utc)) selected = (int)i;
    }
    return selected;
}

uint32_t pdkpass_reminder_wait(const pdkpass_reminder_schedule_t *s,
                              bool time_valid, int64_t now)
{
    if (!s || !s->enabled || !time_valid || now < 1767225600LL) return UINT32_MAX;
    int64_t earliest = INT64_MAX;
    for (size_t i = 0; i < PDKPASS_REMINDER_CAPACITY; i++) {
        const pdkpass_reminder_entry_t *e = &s->entries[i];
        if (eligible(e, now) && e->start_utc < earliest) earliest = e->start_utc;
    }
    if (earliest == INT64_MAX) return UINT32_MAX;
    int64_t seconds = earliest - PDKPASS_REMINDER_LEAD_SECONDS - now;
    if (seconds <= 0) return 0;
    if (seconds > 86400) seconds = 86400;
    return (uint32_t)seconds * 1000U;
}

int pdkpass_reminder_next_session(
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT], int64_t now)
{
    int next = -1;
    if (!entries) return next;
    for (int i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        if (entries[i].start_utc <= now || entries[i].start_utc <= 0 ||
            (entries[i].flags & PDKPASS_REMINDER_CANCELLED)) continue;
        if (next < 0 || entries[i].start_utc < entries[next].start_utc) next = i;
    }
    return next;
}
