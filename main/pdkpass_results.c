#include "pdkpass_cache.h"
#include "pdkpass_results.h"
#include "pdkpass_reminder.h"
#include "pdkpass_sync_policy.h"
#include "pdkpass_network.h"

#include "cJSON.h"
#include "pdkpass_http.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pdkpass_data.h"
#include "pdkpass_season.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESULTS_TASK_STACK 7168
#define RESULTS_TASK_PRIORITY 3
#define RESULTS_ACTIVE_DELAY_MS (10U * 60U * 1000U)
#define RESULTS_BACKFILL_DELAY_MS 5000U
#define RESULTS_IDLE_DELAY_MS (24U * 60U * 60U * 1000U)
#define RESULTS_DISCOVERY_INTERVAL_SECONDS (6LL * 60LL * 60LL)
#define RESULTS_MANUAL_RETRY_SECONDS (5LL * 60LL)
#define RESULTS_PRIORITY_SECONDS (15LL * 60LL)
#define RESULTS_WINDOW_SECONDS (5LL * 24LL * 60LL * 60LL)
#define RESULTS_GRACE_SECONDS (24LL * 60LL * 60LL)
#define RESULTS_FORCE_COOLDOWN_MS 60000U
#define RESULTS_SAVE_RETRY_MS 60000U
#define RESULTS_SAVE_BUSY_RETRY_MS 1000U
#define RESULTS_CACHE_MAGIC 0x50444B52U
#define RESULTS_CACHE_VERSION 3U

#define EVENT_WAKE BIT0

typedef struct {
    uint8_t present;
    uint8_t cancelled;
    uint8_t ready;
    uint8_t reserved;
    int32_t session_key;
    int64_t end_utc;
    int64_t last_attempt_utc;
    char podium_codes[PDKPASS_PODIUM_SIZE][4];
} session_cache_t;

typedef struct {
    int32_t meeting_key;
    uint8_t discovered;
    uint8_t reserved[3];
    int64_t last_discovery_utc;
    int64_t next_discovery_utc;
    session_cache_t sessions[PDKPASS_SESSION_COUNT];
} race_cache_t;

typedef struct {
    int32_t meeting_key;
    uint8_t discovered;
    uint8_t present_mask;
    uint8_t cancelled_mask;
    uint8_t ready_mask;
    char podium_codes[PDKPASS_SESSION_COUNT][PDKPASS_PODIUM_SIZE][4];
    int32_t session_keys[PDKPASS_SESSION_COUNT];
} persisted_race_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t year;
    uint8_t race_count;
    uint8_t reserved[3];
    persisted_race_t races[PDKPASS_MAX_RACES];
} results_store_t;

typedef struct {
    int32_t meeting_key;
    uint8_t discovered, present_mask, cancelled_mask, ready_mask;
    char podium_codes[PDKPASS_SESSION_COUNT][PDKPASS_PODIUM_SIZE][4];
} legacy_race_t;
typedef struct {
    uint32_t magic;
    uint16_t version, year;
    uint8_t race_count, reserved[3];
    legacy_race_t races[PDKPASS_MAX_RACES];
} legacy_store_t;

// One full 24-round season, including seven session podiums per round, stays
// below 3 KB and no longer duplicates driver/team strings.
_Static_assert(sizeof(results_store_t) <= 3072,
               "Season results cache no longer fits the NVS budget");



typedef struct {
    unsigned position;
    int driver_number;
} result_driver_t;

typedef enum {
    PROCESS_IDLE = 0,
    PROCESS_PROGRESS,
    PROCESS_RETRY,
} process_outcome_t;

static const char *TAG = "pdkpass_results";
static const char *NVS_NAMESPACE = "pdk_results";
static const char *NVS_KEY = "season";
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static pdkpass_results_callback_t s_callback;
static bool s_online;
static size_t s_requested_race = SIZE_MAX;
// Worker-owned focus survives individual discovery/podium transactions.
static size_t s_priority_race = SIZE_MAX;
static int64_t s_priority_until_utc;
static size_t s_history_cursor;
static unsigned s_cache_year;
static size_t s_cache_count;
static bool s_cache_dirty;
// Protected by s_lock; separate from the network scheduler and wall clock.
static int64_t s_cache_retry_at_us;
static bool s_has_cached_data;
static bool s_force_pending;
static size_t s_force_race = SIZE_MAX;
static pdkpass_manual_status_t s_force_status;
static TickType_t s_force_last_tick;
static bool s_force_has_last_tick;

#include "pdkpass_results_store.inc"

#include "pdkpass_results_parse.inc"

static int64_t retry_interval_seconds(size_t race_index, int64_t now_utc)
{
    pdkpass_race_t race;
    if (!pdkpass_season_race_get(race_index, &race)) {
        return RESULTS_IDLE_DELAY_MS / 1000U;
    }
    if (race_index == s_priority_race && now_utc < s_priority_until_utc)
        return RESULTS_MANUAL_RETRY_SECONDS;
    return now_utc > race.switch_at_utc + RESULTS_GRACE_SECONDS
               ? RESULTS_IDLE_DELAY_MS / 1000U
               : RESULTS_ACTIVE_DELAY_MS / 1000U;
}

static bool cache_has_due_result(size_t race_index, const race_cache_t *cache,
                                 int64_t now_utc)
{
    int64_t retry_interval = retry_interval_seconds(race_index, now_utc);
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        const session_cache_t *session = &cache->sessions[i];
        if (session->present && !session->cancelled && !session->ready &&
            pdkpass_session_result_due(now_utc, session->end_utc) &&
            now_utc - session->last_attempt_utc >= retry_interval) return true;
    }
    return false;
}

static bool discovery_due(const race_cache_t *cache, int64_t now_utc)
{
    return now_utc >= cache->next_discovery_utc;
}

static bool cache_complete(size_t race_index, const race_cache_t *cache,
                           int64_t now_utc)
{
    pdkpass_race_t race;
    if (!pdkpass_season_race_get(race_index, &race)) return true;
    if (!cache->discovered ||
        now_utc < race.switch_at_utc + RESULTS_GRACE_SECONDS ||
        !cache->sessions[PDKPASS_SESSION_RACE].present) return false;
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        const session_cache_t *session = &cache->sessions[i];
        if (session->present && !session->cancelled && !session->ready) {
            return false;
        }
    }
    return true;
}

static bool race_is_eligible(size_t race_index, int64_t now_utc)
{
    pdkpass_race_t race;
    return pdkpass_season_race_get(race_index, &race) &&
           now_utc >= race.switch_at_utc - RESULTS_WINDOW_SECONDS;
}

static bool race_needs_work(size_t race_index, int64_t now_utc)
{
    if (!race_is_eligible(race_index, now_utc)) return false;
    race_cache_t cache;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    cache = s_cache[race_index];
    xSemaphoreGive(s_lock);
    if (cache_complete(race_index, &cache, now_utc)) return false;
    if (cache_has_due_result(race_index, &cache, now_utc)) return true;
    return discovery_due(&cache, now_utc);
}

static void expedite_requested_race(size_t race_index, int64_t now_utc)
{
    if (now_utc < 1767225600LL || !race_is_eligible(race_index, now_utc))
        return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    race_cache_t *cache = &s_cache[race_index];
    if (!cache_complete(race_index, cache, now_utc)) {
        // Do this in the results worker, after any in-flight request has
        // published its cache. The UI only queues the requested race.
        int64_t retry_at = cache->last_discovery_utc + RESULTS_MANUAL_RETRY_SECONDS;
        if (retry_at < now_utc) retry_at = now_utc;
        if (cache->next_discovery_utc > retry_at)
            cache->next_discovery_utc = retry_at;
        for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
            session_cache_t *session = &cache->sessions[i];
            if (session->present && !session->cancelled && !session->ready &&
                pdkpass_session_result_due(now_utc, session->end_utc) &&
                now_utc - session->last_attempt_utc >= RESULTS_MANUAL_RETRY_SECONDS)
                session->last_attempt_utc = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

static size_t select_race(int64_t now_utc)
{
    size_t race_count = pdkpass_season_race_count();
    size_t requested = SIZE_MAX;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        requested = s_requested_race;
        s_requested_race = SIZE_MAX;
        xSemaphoreGive(s_lock);
    }
    if (requested < race_count) {
        s_priority_race = race_is_eligible(requested, now_utc) ? requested : SIZE_MAX;
        s_priority_until_utc = now_utc + RESULTS_PRIORITY_SECONDS;
        expedite_requested_race(requested, now_utc);
    }
    // A briefly viewed historical round must not keep the radio retrying
    // every five minutes for the rest of the day. Further requests renew it.
    if (now_utc >= s_priority_until_utc) s_priority_race = SIZE_MAX;
    if (s_priority_race < race_count &&
        race_needs_work(s_priority_race, now_utc)) return s_priority_race;
    // While the requested race is waiting, other rounds may make progress.
    // Keep its focus so the next podium/retry is not lost to the active weekend.
    // Active weekend first; historical backfill rotates independently.
    for (size_t i = race_count; i > 0; i--) {
        pdkpass_race_t race;
        if (pdkpass_season_race_get(i - 1, &race) &&
            now_utc <= race.switch_at_utc + RESULTS_GRACE_SECONDS &&
            race_needs_work(i - 1, now_utc)) return i - 1;
    }
    for (size_t offset = 0; offset < race_count; offset++) {
        size_t i = (s_history_cursor + offset) % race_count;
        if (race_needs_work(i, now_utc)) {
            s_history_cursor = (i + 1U) % race_count;
            return i;
        }
    }
    return SIZE_MAX;
}

static void finish_manual_race(size_t race_index, pdkpass_manual_state_t state)
{
    // Worker only; terminal status must not be dropped on lock contention.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_force_race == race_index && s_force_status.state == PDKPASS_MANUAL_RUNNING) {
        s_force_status.state = pdkpass_manual_visible_status(
            s_force_status, esp_timer_get_time()).state == PDKPASS_MANUAL_TIMED_OUT
                ? PDKPASS_MANUAL_TIMED_OUT : state;
        s_force_status.generation++;
        pdkpass_sync_hold(PDKPASS_SYNC_RESULTS, false);
    }
    xSemaphoreGive(s_lock);
}

static void finish_pending_manual_result(size_t race_index, unsigned session)
{
    // An automatic fetch can finish the very session whose manual refresh was
    // queued while HTTP was active. Only newly fetched, persisted results use
    // this path; existing cached podiums still need a check for corrections.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool matching = s_force_pending && s_force_race == race_index &&
                    s_force_status.state == PDKPASS_MANUAL_RUNNING &&
                    s_force_status.session == session;
    if (matching) s_force_pending = false;
    xSemaphoreGive(s_lock);
    // The single results worker owns completion; RUNNING blocks a new request
    // until finish_manual_race publishes the terminal state. HTTP stays held.
    if (matching) finish_manual_race(race_index, PDKPASS_MANUAL_UPDATED);
}

static process_outcome_t process_race(size_t race_index, int64_t now_utc)
{
    pdkpass_race_t race;
    unsigned season_year = pdkpass_season_year();
    if (!pdkpass_season_race_get(race_index, &race)) return PROCESS_RETRY;
    race_cache_t cache;
    pdkpass_race_t current_race;
    if (pdkpass_season_year() != season_year ||
        !pdkpass_season_race_get(race_index, &current_race) ||
        current_race.meeting_key != race.meeting_key) {
        return PROCESS_RETRY;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return PROCESS_RETRY;
    }
    cache = s_cache[race_index];
    xSemaphoreGive(s_lock);

    bool changed = false;
    unsigned fetched_session = PDKPASS_SESSION_COUNT;
    if (discovery_due(&cache, now_utc)) {
        race_cache_t before_discovery = cache;
        bool discovered = discover_sessions(race_index, &cache, now_utc);
        cache.last_discovery_utc = now_utc;
        cache.next_discovery_utc = now_utc + (discovered
            ? RESULTS_DISCOVERY_INTERVAL_SECONDS : retry_interval_seconds(race_index, now_utc));
        before_discovery.last_discovery_utc = cache.last_discovery_utc;
        before_discovery.next_discovery_utc = cache.next_discovery_utc;
        changed = memcmp(&before_discovery, &cache, sizeof(cache)) != 0;
    }

    process_outcome_t outcome = changed ? PROCESS_PROGRESS : PROCESS_IDLE;
    int64_t retry_interval = retry_interval_seconds(race_index, now_utc);
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        session_cache_t *session = &cache.sessions[i];
        if (!session->present || session->session_key <= 0 || session->cancelled || session->ready ||
            !pdkpass_session_result_due(now_utc, session->end_utc) ||
            now_utc - session->last_attempt_utc < retry_interval) continue;
        session->last_attempt_utc = now_utc;
        if (fetch_result(session)) {
            fetched_session = (unsigned)i;
            changed = true;
            outcome = PROCESS_PROGRESS;
        } else {
            outcome = PROCESS_RETRY;
        }
        break;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return PROCESS_RETRY;
    }
    s_cache[race_index] = cache;
    xSemaphoreGive(s_lock);

    s_cache_dirty |= changed;
    if (s_cache_dirty) {
        save_cache_with_retry();
    }
    bool new_result = fetched_session < PDKPASS_SESSION_COUNT && !s_cache_dirty;
    if (new_result) {
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_RESULTS, (int64_t)time(NULL));
        finish_pending_manual_result(race_index, fetched_session);
    }
    if (changed && s_callback) s_callback(race_index, new_result);
    return outcome;
}

static bool take_manual_race(size_t *race_index)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool pending = s_force_pending;
    if (pending) {
        *race_index = s_force_race;
        s_force_pending = false;
    }
    xSemaphoreGive(s_lock);
    return pending;
}

static bool manual_race_pending(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool pending = s_force_pending;
    xSemaphoreGive(s_lock);
    return pending;
}

static int64_t manual_deadline(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t deadline = s_force_status.deadline_us;
    xSemaphoreGive(s_lock);
    return deadline;
}

static bool persisted_race_changed(const race_cache_t *before,
                                   const race_cache_t *after)
{
    if (before->meeting_key != after->meeting_key ||
        before->discovered != after->discovered) return true;
    for (size_t i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        const session_cache_t *a = &before->sessions[i];
        const session_cache_t *b = &after->sessions[i];
        if (a->session_key != b->session_key || a->present != b->present ||
            a->cancelled != b->cancelled || a->ready != b->ready ||
            memcmp(a->podium_codes, b->podium_codes,
                   sizeof(a->podium_codes)) != 0) return true;
    }
    return false;
}

static void process_manual_race(size_t race_index, int64_t now_utc)
{
    pdkpass_race_t race;
    unsigned year = pdkpass_season_year();
    if (!pdkpass_season_race_get(race_index, &race) ||
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        finish_manual_race(race_index, PDKPASS_MANUAL_FAILED);
        return;
    }
    race_cache_t before = s_cache[race_index];
    unsigned selected_session = s_force_status.session;
    xSemaphoreGive(s_lock);
    if (selected_session >= PDKPASS_SESSION_COUNT) {
        finish_manual_race(race_index, PDKPASS_MANUAL_NOT_READY);
        return;
    }
    race_cache_t candidate = before;
    bool failed = !discover_sessions(race_index, &candidate, now_utc);
    // Discovery locates the selected session; other cached results stay intact.
    for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        if (i != selected_session) candidate.sessions[i] = before.sessions[i];
    }
    bool due = false, updated = false;
    unsigned first_results = 0;
    do {
        session_cache_t *session = &candidate.sessions[selected_session];
        if (!session->present || session->cancelled || session->session_key <= 0 ||
            session->end_utc <= 0 || now_utc < session->end_utc) continue;
        due = true;
        session_cache_t checked = *session;
        checked.last_attempt_utc = now_utc;
        if (!fetch_result(&checked)) {
            failed = true;
            continue;
        }
        if (!session->ready) first_results++;
        updated |= !session->ready ||
                   memcmp(session->podium_codes, checked.podium_codes,
                          sizeof(session->podium_codes)) != 0;
        *session = checked;
    } while (false);

    pdkpass_race_t current;
    if (pdkpass_season_year() != year ||
        !pdkpass_season_race_get(race_index, &current) ||
        current.meeting_key != race.meeting_key) {
        finish_manual_race(race_index, PDKPASS_MANUAL_FAILED);
        return;
    }
    if (pdkpass_http_expired()) {
        finish_manual_race(race_index, PDKPASS_MANUAL_TIMED_OUT);
        return;
    }
    bool changed = memcmp(&before, &candidate, sizeof(candidate)) != 0;
    bool needs_save = persisted_race_changed(&before, &candidate);
    if (changed) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            finish_manual_race(race_index, PDKPASS_MANUAL_FAILED);
            return;
        }
        s_cache[race_index] = candidate;
        xSemaphoreGive(s_lock);
        if (needs_save) {
            s_cache_dirty = true;
            if (save_cache_with_retry() != ESP_OK) failed = true;
        }
    }
    pdkpass_manual_state_t state = !due ?
        (failed ? PDKPASS_MANUAL_FAILED : PDKPASS_MANUAL_NOT_READY) :
        failed ? (updated && !s_cache_dirty ? PDKPASS_MANUAL_PARTIAL : PDKPASS_MANUAL_FAILED) :
        updated ? PDKPASS_MANUAL_UPDATED : PDKPASS_MANUAL_UNCHANGED;
    if ((state == PDKPASS_MANUAL_UPDATED || state == PDKPASS_MANUAL_UNCHANGED) &&
        !s_cache_dirty && !pdkpass_http_expired())
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_RESULTS, (int64_t)time(NULL));
    finish_manual_race(race_index, state);
    // Publish completion before waking the UI with the new podium.
    if (changed && s_callback) {
        if (first_results && !s_cache_dirty) {
            // Each newly cached session keeps its own completion cue.
            for (unsigned i = 0; i < first_results; i++) {
                s_callback(race_index, true);
                if (i + 1U < first_results) vTaskDelay(pdMS_TO_TICKS(120));
            }
        } else s_callback(race_index, false);
    }
}

static TickType_t next_scheduled_wait(int64_t now_utc)
{
    int64_t best_seconds = RESULTS_IDLE_DELAY_MS / 1000U;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return pdMS_TO_TICKS(RESULTS_ACTIVE_DELAY_MS);
    }
    size_t race_count = pdkpass_season_race_count();
    for (size_t i = 0; i < race_count; i++) {
        pdkpass_race_t race;
        if (!pdkpass_season_race_get(i, &race)) continue;
        int64_t start = race.switch_at_utc - RESULTS_WINDOW_SECONDS;
        if (now_utc < start) {
            int64_t until_start = start - now_utc;
            if (until_start < best_seconds) best_seconds = until_start;
            continue;
        }

        const race_cache_t *cache = &s_cache[i];
        if (cache_complete(i, cache, now_utc)) continue;
        int64_t discovery_at = cache->next_discovery_utc;
        if (discovery_at <= now_utc) {
            best_seconds = 1;
        } else if (discovery_at - now_utc < best_seconds) {
            best_seconds = discovery_at - now_utc;
        }

        int64_t retry_interval = retry_interval_seconds(i, now_utc);
        for (size_t session_index = 0;
             session_index < PDKPASS_SESSION_COUNT; session_index++) {
            const session_cache_t *session = &cache->sessions[session_index];
            if (!session->present || session->end_utc <= 0 || session->cancelled || session->ready) continue;
            int64_t due_at = session->end_utc + PDKPASS_RESULT_DELAY_SECONDS;
            if (due_at <= now_utc) {
                due_at = session->last_attempt_utc + retry_interval;
            }
            if (due_at <= now_utc) {
                best_seconds = 1;
            } else if (due_at - now_utc < best_seconds) {
                best_seconds = due_at - now_utc;
            }
        }
    }
    xSemaphoreGive(s_lock);
    if (best_seconds < 1) best_seconds = 1;
    return pdMS_TO_TICKS((uint32_t)best_seconds * 1000U);
}

static bool request_pending(void)
{
    bool pending = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        pending = s_requested_race != SIZE_MAX;
        xSemaphoreGive(s_lock);
    }
    return pending;
}

static bool online_snapshot(void)
{
    bool online = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        online = s_online;
        xSemaphoreGive(s_lock);
    }
    return online;
}

static void results_task(void *arg)
{
    (void)arg;
    bool manual_connect_requested = false;
    TickType_t delay = pdMS_TO_TICKS(RESULTS_IDLE_DELAY_MS);
    for (;;) {
        xEventGroupWaitBits(s_events, EVENT_WAKE, pdTRUE, pdFALSE, cache_retry_wait_ticks(delay));
        retry_cache_if_due();
        // A UI request may arrive inside HTTP work, before that work writes
        // its next deadline. The queued request must override that stale wait.
        uint32_t wait_ms = (manual_race_pending() || request_pending()) ? 0 :
                           pdkpass_sync_wait_ms(PDKPASS_SYNC_RESULTS);
        if (!online_snapshot()) {
            if (manual_race_pending()) {
                uint32_t connect_wait = pdkpass_manual_connect_wait_ms(
                    manual_deadline(), esp_timer_get_time());
                bool failed = connect_wait && !manual_connect_requested &&
                    pdkpass_network_request(PDKPASS_NETWORK_RETRY) != ESP_OK;
                if (!connect_wait || failed) {
                    size_t manual_race;
                    if (take_manual_race(&manual_race)) finish_manual_race(manual_race,
                        failed ? PDKPASS_MANUAL_OFFLINE : PDKPASS_MANUAL_TIMED_OUT);
                    manual_connect_requested = false;
                } else {
                    manual_connect_requested = true;
                    delay = pdMS_TO_TICKS(connect_wait);
                    continue;
                }
            } else manual_connect_requested = false;
            if (!wait_ms) pdkpass_network_request(PDKPASS_NETWORK_SYNC);
            delay = wait_ms ? pdMS_TO_TICKS(wait_ms) : portMAX_DELAY;
            continue;
        }
        manual_connect_requested = false;
        if (wait_ms) { delay = pdMS_TO_TICKS(wait_ms); continue; }

        size_t manual_race;
        if (take_manual_race(&manual_race)) {
            if (pdkpass_http_begin_until(manual_deadline())) {
                if (online_snapshot()) {
                    if (s_cache_dirty && !pdkpass_http_expired()) save_cache_with_retry();
                    process_manual_race(manual_race, (int64_t)time(NULL));
                } else finish_manual_race(manual_race, PDKPASS_MANUAL_OFFLINE);
                pdkpass_http_end();
            } else finish_manual_race(manual_race, PDKPASS_MANUAL_TIMED_OUT);
            delay = next_scheduled_wait((int64_t)time(NULL));
            pdkpass_sync_plan(PDKPASS_SYNC_RESULTS, delay * portTICK_PERIOD_MS);
            continue;
        }
        pdkpass_http_begin();
        if (!online_snapshot()) { pdkpass_http_end(); delay = 1; continue; }
        int64_t now_utc = (int64_t)time(NULL);
        if (s_cache_dirty) save_cache_with_retry();
        size_t race_index = select_race(now_utc);
        size_t race_count = pdkpass_season_race_count();
        if (race_index >= race_count) {
            delay = next_scheduled_wait(now_utc);
            pdkpass_sync_plan(PDKPASS_SYNC_RESULTS, delay * portTICK_PERIOD_MS);
            pdkpass_http_end();
            continue;
        }

        process_race(race_index, now_utc);
        delay = next_scheduled_wait((int64_t)time(NULL));
        if (delay < pdMS_TO_TICKS(RESULTS_BACKFILL_DELAY_MS)) {
            delay = pdMS_TO_TICKS(RESULTS_BACKFILL_DELAY_MS);
        }
        pdkpass_sync_plan(PDKPASS_SYNC_RESULTS, delay * portTICK_PERIOD_MS);
        pdkpass_http_end();
    }
}

esp_err_t pdkpass_results_start(pdkpass_results_callback_t callback)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    if (pdkpass_http_init() != ESP_OK) return ESP_ERR_NO_MEM;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) {
        if (s_events) vEventGroupDelete(s_events);
        s_events = NULL;
        pdkpass_http_report_data_failure("results-startup", ESP_ERR_NO_MEM, 0);
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    load_cache();
    if (xTaskCreate(results_task, "pdk_results", RESULTS_TASK_STACK, NULL,
                    RESULTS_TASK_PRIORITY, NULL) != pdPASS) {
        // Preserve readable caches, but never accept work without a worker.
        vEventGroupDelete(s_events);
        s_events = NULL;
        pdkpass_http_report_data_failure("results-task", ESP_ERR_NO_MEM, RESULTS_TASK_STACK);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_results_set_online(bool online)
{
    if (!s_lock || !s_events) return;
    bool changed = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        changed = s_online != online;
        s_online = online;
        xSemaphoreGive(s_lock);
    }
    // Setup countdown/status publications repeat the same offline state. Waking
    // results for each one feeds SYNC back to the network worker indefinitely.
    if (changed) xEventGroupSetBits(s_events, EVENT_WAKE);
}

void pdkpass_results_season_changed(void)
{
    if (!s_lock || !s_events) return;
    // Called by the season worker within the shared HTTP transaction. It
    // cannot interleave with process_race or a results NVS write.
    bool same = s_cache_year == pdkpass_season_year() &&
                s_cache_count == pdkpass_season_race_count();
    for (size_t i = 0; same && i < s_cache_count; i++) {
        pdkpass_race_t race;
        same = pdkpass_season_race_get(i, &race) && race.meeting_key == s_cache[i].meeting_key;
    }
    if (!same) {
        load_cache();
        pdkpass_sync_plan(PDKPASS_SYNC_RESULTS, 0);
    }
    xEventGroupSetBits(s_events, EVENT_WAKE);
}

void pdkpass_results_request_race(size_t race_index)
{
    if (!s_lock || !s_events ||
        race_index >= pdkpass_season_race_count()) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_requested_race = race_index;
        bool needs_sync = !cache_complete(race_index, &s_cache[race_index],
                                          (int64_t)time(NULL));
        xSemaphoreGive(s_lock);
        if (needs_sync) pdkpass_sync_plan(PDKPASS_SYNC_RESULTS, 0);
    }
    xEventGroupSetBits(s_events, EVENT_WAKE);
}

pdkpass_manual_state_t pdkpass_results_force_session(size_t race_index, pdkpass_session_kind_t session)
{
    if (!s_lock || !s_events || session >= PDKPASS_SESSION_COUNT ||
        race_index >= pdkpass_season_race_count() ||
        !race_is_eligible(race_index, (int64_t)time(NULL)))
        return PDKPASS_MANUAL_NOT_READY;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return PDKPASS_MANUAL_BUSY;
    TickType_t now = xTaskGetTickCount();
    pdkpass_manual_state_t result = PDKPASS_MANUAL_RUNNING;
    if (s_force_status.state == PDKPASS_MANUAL_RUNNING)
        result = PDKPASS_MANUAL_BUSY;
    else if (s_force_has_last_tick &&
             now - s_force_last_tick < pdMS_TO_TICKS(RESULTS_FORCE_COOLDOWN_MS))
        result = PDKPASS_MANUAL_COOLDOWN;
    else {
        s_force_has_last_tick = true;
        s_force_last_tick = now;
        s_force_race = race_index;
        s_force_status.session = session;
        s_force_pending = true;
        s_force_status.cooldown_until_us = esp_timer_get_time() + 60000000LL;
        s_force_status.deadline_us = esp_timer_get_time() + PDKPASS_MANUAL_TIMEOUT_US;
        s_force_status.state = PDKPASS_MANUAL_RUNNING;
        s_force_status.generation++;
        pdkpass_sync_hold(PDKPASS_SYNC_RESULTS, true);
    }
    xSemaphoreGive(s_lock);
    if (result == PDKPASS_MANUAL_RUNNING) {
        xEventGroupSetBits(s_events, EVENT_WAKE);
    }
    return result;
}

bool pdkpass_results_manual_status(size_t *race_index,
                                   pdkpass_manual_status_t *status)
{
    if (!race_index || !status || !s_lock ||
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    *race_index = s_force_race;
    *status = pdkpass_manual_visible_status(s_force_status, esp_timer_get_time());
    xSemaphoreGive(s_lock);
    return true;
}

bool pdkpass_results_get(size_t race_index, pdkpass_session_kind_t session,
                         pdkpass_result_snapshot_t *snapshot)
{
    if (!snapshot || !s_lock ||
        race_index >= pdkpass_season_race_count() ||
        race_index >= PDKPASS_MAX_RACES || session >= PDKPASS_SESSION_COUNT) {
        return false;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    const race_cache_t *race = &s_cache[race_index];
    session_cache_t cached = race->sessions[session];
    bool discovered = race->discovered;
    xSemaphoreGive(s_lock);

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->session_end_utc = cached.end_utc;
    if (cached.ready) {
        snapshot->status = PDKPASS_RESULT_READY;
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            pdkpass_podium_driver_t *podium = &snapshot->podium[i];
            podium->position = (unsigned)(i + 1U);
            snprintf(podium->code, sizeof(podium->code), "%s",
                     cached.podium_codes[i]);
            pdkpass_driver_t driver;
            if (pdkpass_season_driver_by_code(podium->code, &driver)) {
                snprintf(podium->name, sizeof(podium->name), "%s", driver.name);
                snprintf(podium->team, sizeof(podium->team), "%.*s",
                         (int)sizeof(podium->team) - 1, driver.team);
            } else {
                memcpy(podium->name, cached.podium_codes[i],
                       sizeof(cached.podium_codes[i]));
                snprintf(podium->team, sizeof(podium->team), "TEAM");
            }
        }
    } else if (cached.cancelled) {
        snapshot->status = PDKPASS_RESULT_CANCELLED;
    } else if (cached.present) {
        snapshot->status = PDKPASS_RESULT_SCHEDULED;
    } else if (discovered) {
        snapshot->status = PDKPASS_RESULT_NOT_HELD;
    } else {
        snapshot->status = PDKPASS_RESULT_UNKNOWN;
    }
    return true;
}

bool pdkpass_results_has_cached_data(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool cached = s_has_cached_data;
    xSemaphoreGive(s_lock);
    return cached;
}
