#include "pdkpass_cache.h"
#include "pdkpass_reminder.h"
#include "pdkpass_season.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>

#define REMINDER_MAGIC 0x5044524DU
#define REMINDER_VERSION 1U
static const char *TAG = "pdk_reminder";
typedef struct {
    uint32_t magic;
    uint16_t version, reserved;
    pdkpass_reminder_schedule_t schedule;
} reminder_store_t;
_Static_assert(sizeof(reminder_store_t) <= 3072, "Reminder cache exceeds budget");
static reminder_store_t s_store;
static SemaphoreHandle_t s_lock;
static void (*s_wake)(void);
static atomic_bool s_time_valid;
static atomic_bool s_enabled = true;
static bool s_dirty;
static atomic_bool s_calendar_changed = true;
static int64_t s_retry_utc;

static esp_err_t persist(void)
{
    return pdkpass_cache_write_blob("pdk_reminder", "schedule", &s_store, sizeof(s_store));
}

esp_err_t pdkpass_reminder_init(void (*wake_worker)(void))
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_wake = wake_worker;
    size_t size = sizeof(s_store);
    esp_err_t err = pdkpass_cache_read_blob("pdk_reminder", "schedule", &s_store, &size);
    if (err != ESP_OK || size != sizeof(s_store) ||
        s_store.magic != REMINDER_MAGIC || s_store.version != REMINDER_VERSION ||
        s_store.schedule.year > 2100 || s_store.schedule.enabled > 1) {
        memset(&s_store, 0, sizeof(s_store));
        s_store.magic = REMINDER_MAGIC;
        s_store.version = REMINDER_VERSION;
        s_store.schedule.enabled = 1;
    }
    atomic_store(&s_enabled, s_store.schedule.enabled != 0);
    return ESP_OK;
}

void pdkpass_reminder_set_time_valid(bool valid)
{
    atomic_store(&s_time_valid, valid);
    if (s_wake) s_wake();
}

void pdkpass_reminder_season_changed(void)
{
    atomic_store(&s_calendar_changed, true);
    if (s_wake) s_wake();
}

static void reconcile_calendar(void)
{
    if (!atomic_exchange(&s_calendar_changed, false)) return;
    unsigned year = pdkpass_season_year();
    if (year != s_store.schedule.year) {
        if (year > s_store.schedule.year) {
            memset(s_store.schedule.entries, 0, sizeof(s_store.schedule.entries));
            memset(s_store.schedule.meeting_keys, 0, sizeof(s_store.schedule.meeting_keys));
            s_store.schedule.year = (uint16_t)year;
            s_dirty = true;
        }
        return;
    }
    // A downloaded calendar identifies every meeting. A bundled calendar can
    // have zero keys, so it must not invalidate persisted API identities.
    bool complete = pdkpass_season_has_cached_data();
    size_t count = pdkpass_season_race_count();
    for (size_t i = 0; i < count; i++) {
        pdkpass_race_t race;
        if (!pdkpass_season_race_get(i, &race) || race.meeting_key <= 0) complete = false;
    }
    for (size_t slot = 0; slot < PDKPASS_MAX_RACES; slot++) {
        int32_t key = s_store.schedule.meeting_keys[slot];
        if (key <= 0) continue;
        unsigned round = 0;
        for (size_t i = 0; i < count; i++) {
            pdkpass_race_t race;
            if (pdkpass_season_race_get(i, &race) && race.meeting_key == key) {
                round = race.round;
                break;
            }
        }
        for (size_t kind = 0; kind < PDKPASS_SESSION_COUNT; kind++) {
            pdkpass_reminder_entry_t *e = &s_store.schedule.entries[slot * PDKPASS_SESSION_COUNT + kind];
            if (round && e->round != round) { e->round = round; s_dirty = true; }
            if (!round && complete && !(e->flags & PDKPASS_REMINDER_CANCELLED)) {
                e->flags |= PDKPASS_REMINDER_CANCELLED;
                s_dirty = true;
            }
        }
    }
}

bool pdkpass_reminder_enabled(void) { return atomic_load(&s_enabled); }

void pdkpass_reminder_set_enabled(bool enabled)
{
    atomic_store(&s_enabled, enabled);
    if (s_wake) s_wake();
}

void pdkpass_reminder_update_round(unsigned year, unsigned round, int32_t meeting_key,
    const pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT])
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    if (pdkpass_reminder_merge(&s_store.schedule, year, round, meeting_key, entries)) s_dirty = true;
    xSemaphoreGive(s_lock);
    if (s_wake) s_wake();
}

bool pdkpass_reminder_poll(int64_t now, pdkpass_reminder_entry_t *alert)
{
    if (!alert || !s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return false;
    reconcile_calendar();
    bool enabled = atomic_load(&s_enabled);
    if (s_store.schedule.enabled != enabled) {
        s_store.schedule.enabled = enabled;
        s_dirty = true;
    }
    bool delivered = false;
    if (now < s_retry_utc && s_retry_utc - now <= 5) goto done;
    int index = pdkpass_reminder_due(&s_store.schedule, atomic_load(&s_time_valid), now);
    if (index >= 0) {
        s_store.schedule.entries[index].flags |= PDKPASS_REMINDER_FIRED;
        s_dirty = true;
    }
    if (s_dirty) {
        esp_err_t err = persist();
        if (err != ESP_OK) {
            if (index >= 0) s_store.schedule.entries[index].flags &= ~PDKPASS_REMINDER_FIRED;
            s_retry_utc = now + 5;
            ESP_LOGW(TAG, "Reminder persistence failed: %s", esp_err_to_name(err));
            goto done;
        }
        s_dirty = false;
    }
    s_retry_utc = 0;
    if (index >= 0) {
        *alert = s_store.schedule.entries[index];
        delivered = true;
    }
done:
    xSemaphoreGive(s_lock);
    return delivered;
}

uint32_t pdkpass_reminder_wait_ms(int64_t now)
{
    if (!s_lock) return UINT32_MAX;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return 1000;
    uint32_t wait = pdkpass_reminder_wait(&s_store.schedule,
                                        atomic_load(&s_time_valid), now);
    if (s_dirty || atomic_load(&s_calendar_changed) ||
        s_store.schedule.enabled != atomic_load(&s_enabled)) wait = 0;
    if (s_retry_utc > now && s_retry_utc - now <= 5)
        wait = (uint32_t)(s_retry_utc - now) * 1000U;
    xSemaphoreGive(s_lock);
    return wait;
}

bool pdkpass_reminder_round_schedule(unsigned year, int32_t meeting_key,
    pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT])
{
    if (!entries || meeting_key <= 0 || !s_lock ||
        xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
    bool found = false;
    if (s_store.schedule.year == year) {
        for (size_t i = 0; i < PDKPASS_MAX_RACES; i++) {
            if (s_store.schedule.meeting_keys[i] != meeting_key) continue;
            memcpy(entries, &s_store.schedule.entries[i * PDKPASS_SESSION_COUNT],
                   sizeof(*entries) * PDKPASS_SESSION_COUNT);
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}
