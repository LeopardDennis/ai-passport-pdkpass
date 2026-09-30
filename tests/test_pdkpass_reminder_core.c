#include "pdkpass_reminder_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    pdkpass_reminder_entry_t upcoming[PDKPASS_SESSION_COUNT] = {0};
    upcoming[PDKPASS_SESSION_RACE].start_utc = 300;
    upcoming[PDKPASS_SESSION_FP2].start_utc = 100;
    upcoming[PDKPASS_SESSION_SPRINT].start_utc = 200;
    upcoming[PDKPASS_SESSION_FP2].flags = PDKPASS_REMINDER_FIRED;
    assert(pdkpass_reminder_next_session(upcoming, 99) == PDKPASS_SESSION_FP2);
    assert(pdkpass_reminder_next_session(upcoming, 100) == PDKPASS_SESSION_SPRINT);
    upcoming[PDKPASS_SESSION_SPRINT].flags = PDKPASS_REMINDER_CANCELLED;
    assert(pdkpass_reminder_next_session(upcoming, 100) == PDKPASS_SESSION_RACE);
    assert(pdkpass_reminder_next_session(upcoming, 300) == -1);
    assert(pdkpass_reminder_next_session(NULL, 0) == -1);
    const int64_t start = 1790400000LL;
    pdkpass_reminder_schedule_t s = {.enabled = 1};
    pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT] = {0};
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        entries[i].session_key = 100 + (int32_t)i;
        entries[i].start_utc = start + (int64_t)i * 7200;
    }
    assert(pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
    assert(sizeof(s) <= 3072);
    assert(pdkpass_reminder_due(&s, false, start - 600) == -1);
    assert(pdkpass_reminder_wait(&s, false, start - 600) == UINT32_MAX);
    assert(pdkpass_reminder_due(&s, true, start - 601) == -1);
    assert(pdkpass_reminder_wait(&s, true, start - 601) == 1000);
    for (size_t kind = 0; kind < PDKPASS_SESSION_COUNT; kind++) {
        int64_t when = entries[kind].start_utc;
        int index = 14 * PDKPASS_SESSION_COUNT + (int)kind;
        assert(pdkpass_reminder_due(&s, true, when - 600) == index);
        assert(pdkpass_reminder_due(&s, true, when - 1) == index); // late-sync catch-up
        assert(pdkpass_reminder_due(&s, true, when) == -1); // no reminder after start
        s.entries[index].flags |= PDKPASS_REMINDER_FIRED;
        pdkpass_reminder_schedule_t reloaded = s;
        assert(pdkpass_reminder_due(&reloaded, true, when - 600) == -1);
        assert(!pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
        assert(pdkpass_reminder_due(&s, true, when - 600) == -1);
    }
    // Changed start time before delivery updates the deadline; after delivery
    // reconnects, rescheduling and backwards clock jumps do not ring twice.
    memset(&s, 0, sizeof(s));s.enabled=1;
    assert(pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
    entries[0].start_utc += 1800;
    assert(pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
    assert(pdkpass_reminder_due(&s, true, start - 600) == -1);
    assert(pdkpass_reminder_due(&s, true, start + 1200) >= 0);
    entries[0].flags = PDKPASS_REMINDER_CANCELLED;entries[0].start_utc = 0;
    assert(pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
    assert(pdkpass_reminder_due(&s, true, start + 1200) == -1);
    // Missing FP3 on a sprint weekend is removed; no made-up sessions.
    entries[PDKPASS_SESSION_FP3] = (pdkpass_reminder_entry_t){0};
    assert(pdkpass_reminder_merge(&s, 2026, 15, 1295, entries));
    assert(pdkpass_reminder_due(&s, true, start + 2*7200 - 600) == -1);
    s.enabled=0;
    assert(pdkpass_reminder_due(&s,true,start+3*7200-600)==-1);
    s.enabled=1;
    assert(pdkpass_reminder_due(&s,true,start+3*7200-600)>=0);
    pdkpass_reminder_schedule_t before=s;
    assert(!pdkpass_reminder_merge(&s,2025,15,1295,entries));
    assert(!pdkpass_reminder_merge(&s,2026,25,1295,entries));
    assert(!memcmp(&before,&s,sizeof(s)));
    entries[0].flags=0;entries[0].start_utc=start;
    assert(pdkpass_reminder_merge(&s,2027,1,1400,entries));
    assert(s.year==2027 && s.entries[0].session_key==100);
    for(size_t i=PDKPASS_SESSION_COUNT;i<PDKPASS_REMINDER_CAPACITY;i++)
        assert(s.entries[i].session_key==0);
    puts("All-session reminders, deadline, cancellation, persistence identity and clock boundaries: PASS");
}
