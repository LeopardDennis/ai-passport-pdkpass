#include "pdkpass_results.h"

#include "cJSON.h"
#include "pdkpass_http.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pdkpass_data.h"
#include "pdkpass_season.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESULTS_TASK_STACK 7168
#define RESULTS_TASK_PRIORITY 3
#define RESULTS_BODY_LIMIT 24576
#define RESULTS_ACTIVE_DELAY_MS (10U * 60U * 1000U)
#define RESULTS_BACKFILL_DELAY_MS 5000U
#define RESULTS_IDLE_DELAY_MS (24U * 60U * 60U * 1000U)
#define RESULTS_DISCOVERY_INTERVAL_SECONDS (6LL * 60LL * 60LL)
#define RESULTS_WINDOW_SECONDS (5LL * 24LL * 60LL * 60LL)
#define RESULTS_GRACE_SECONDS (24LL * 60LL * 60LL)
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
static size_t s_history_cursor;
static unsigned s_cache_year;
static size_t s_cache_count;
static bool s_cache_dirty;

static void reset_race_cache(size_t race_index, race_cache_t *cache)
{
    memset(cache, 0, sizeof(*cache));
    pdkpass_race_t race;
    if (pdkpass_season_race_get(race_index, &race)) {
        cache->meeting_key = race.meeting_key;
    }
}

static bool persisted_matches(const results_store_t *stored, unsigned year,
                              size_t race_count)
{
    (void)race_count;
    return stored && stored->magic == RESULTS_CACHE_MAGIC &&
           stored->version == RESULTS_CACHE_VERSION && stored->year == year &&
           stored->race_count > 0 && stored->race_count <= PDKPASS_MAX_RACES;
}

static int stored_index(const results_store_t *stored, size_t index,
                         const race_cache_t *race)
{
    if (race->meeting_key == 0) {
        return index < stored->race_count && stored->races[index].meeting_key == 0
                   ? (int)index : -1;
    }
    for (size_t i = 0; i < stored->race_count; i++) {
        if (stored->races[i].meeting_key == race->meeting_key) return (int)i;
    }
    return -1;
}

static void load_cache(void)
{
    race_cache_t *loaded = calloc(PDKPASS_MAX_RACES, sizeof(*loaded));
    results_store_t *stored = malloc(sizeof(*stored));
    if (!loaded || !stored) {
        free(loaded);
        free(stored);
        return;
    }
    for (size_t i = 0; i < PDKPASS_MAX_RACES; i++) {
        reset_race_cache(i, &loaded[i]);
    }

    bool valid = false;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(*stored);
        esp_err_t err = nvs_get_blob(handle, NVS_KEY, stored, &size);
        if (err == ESP_OK && size == sizeof(legacy_store_t)) {
            legacy_store_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                memcpy(legacy, stored, sizeof(*legacy));
                if (legacy->magic == RESULTS_CACHE_MAGIC && legacy->version == 2U) {
                    memset(stored, 0, sizeof(*stored));
                    stored->magic = legacy->magic;
                    stored->version = RESULTS_CACHE_VERSION;
                    stored->year = legacy->year;
                    stored->race_count = legacy->race_count;
                    for (size_t i = 0; i < PDKPASS_MAX_RACES; i++) {
                        memcpy(&stored->races[i], &legacy->races[i], sizeof(legacy_race_t));
                    }
                    size = sizeof(*stored);
                }
                free(legacy);
            }
        }
        nvs_close(handle);
        size_t race_count = pdkpass_season_race_count();
        valid = err == ESP_OK && size == sizeof(*stored) &&
                persisted_matches(stored, pdkpass_season_year(), race_count);
        if (valid) {
            for (size_t i = 0; i < race_count; i++) {
                int match = stored_index(stored, i, &loaded[i]);
                if (match < 0) continue;
                const persisted_race_t *source = &stored->races[match];
                loaded[i].discovered = source->discovered;
                for (size_t session = 0; session < PDKPASS_SESSION_COUNT;
                     session++) {
                    loaded[i].sessions[session].session_key = source->session_keys[session];
                    uint8_t bit = (uint8_t)(1U << session);
                    loaded[i].sessions[session].present =
                        (source->present_mask & bit) != 0U;
                    loaded[i].sessions[session].cancelled =
                        (source->cancelled_mask & bit) != 0U;
                    loaded[i].sessions[session].ready =
                        (source->ready_mask & bit) != 0U;
                    memcpy(loaded[i].sessions[session].podium_codes,
                           source->podium_codes[session],
                           sizeof(loaded[i].sessions[session].podium_codes));
                }
            }
        }
    }

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        memcpy(s_cache, loaded, sizeof(s_cache));
        s_cache_year = pdkpass_season_year();
        s_cache_count = pdkpass_season_race_count();
        s_cache_dirty = false;
        s_requested_race = SIZE_MAX;
        xSemaphoreGive(s_lock);
    }
    if (!valid && nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_erase_all(handle) == ESP_OK) nvs_commit(handle);
        nvs_close(handle);
    }
    free(loaded);
    free(stored);
}

static esp_err_t save_cache(void)
{
    results_store_t *stored = calloc(1, sizeof(*stored));
    if (!stored) return ESP_ERR_NO_MEM;
    stored->magic = RESULTS_CACHE_MAGIC;
    stored->version = RESULTS_CACHE_VERSION;
    stored->year = (uint16_t)pdkpass_season_year();
    size_t race_count = pdkpass_season_race_count();
    if (race_count > PDKPASS_MAX_RACES) race_count = PDKPASS_MAX_RACES;
    stored->race_count = (uint8_t)race_count;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        free(stored);
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < race_count; i++) {
        stored->races[i].meeting_key = s_cache[i].meeting_key;
        stored->races[i].discovered = s_cache[i].discovered;
        for (size_t session = 0; session < PDKPASS_SESSION_COUNT; session++) {
            stored->races[i].session_keys[session] = s_cache[i].sessions[session].session_key;
            uint8_t bit = (uint8_t)(1U << session);
            if (s_cache[i].sessions[session].present) {
                stored->races[i].present_mask |= bit;
            }
            if (s_cache[i].sessions[session].cancelled) {
                stored->races[i].cancelled_mask |= bit;
            }
            if (s_cache[i].sessions[session].ready) {
                stored->races[i].ready_mask |= bit;
            }
            memcpy(stored->races[i].podium_codes[session],
                   s_cache[i].sessions[session].podium_codes,
                   sizeof(stored->races[i].podium_codes[session]));
        }
    }
    xSemaphoreGive(s_lock);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY, stored, sizeof(*stored));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(stored);
    return err;
}

static esp_err_t http_get_json(const char *url, char **json)
{
    return pdkpass_http_get(url, RESULTS_BODY_LIMIT, json);
}

static bool json_bool(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsTrue(item);
}

static void update_session_identity(session_cache_t *session, int32_t key,
                                     bool cancelled, int64_t end_utc)
{
    // Legacy caches have no key. Their same-meeting/kind podium is retained
    // when binding a key for the first time during migration.
    if (session->session_key != 0 && session->session_key != key) {
        memset(session, 0, sizeof(*session));
    }
    session->session_key = key;
    session->present = 1;
    session->cancelled = cancelled;
    if (cancelled) session->ready = 0;
    session->end_utc = end_utc;
}

static bool discover_sessions(size_t race_index, race_cache_t *cache,
                              int64_t now_utc)
{
    pdkpass_race_t race;
    if (!pdkpass_season_race_get(race_index, &race)) return false;
    char url[256];
    if (race.meeting_key > 0) {
        snprintf(url, sizeof(url),
                 "https://api.openf1.org/v1/sessions?meeting_key=%ld",
                 (long)race.meeting_key);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.openf1.org/v1/sessions?year=%u&country_name=%s",
                 pdkpass_season_year(), race.api_country);
    }

    char *body = NULL;
    if (http_get_json(url, &body) != ESP_OK) return false;
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    bool matched = false;
    int64_t window_start = race.switch_at_utc - RESULTS_WINDOW_SECONDS;
    const cJSON *item;
    cJSON_ArrayForEach(item, root) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "session_name");
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(item, "session_key");
        const cJSON *date_end = cJSON_GetObjectItemCaseSensitive(item, "date_end");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(key) ||
            !cJSON_IsString(date_end)) continue;

        pdkpass_session_kind_t kind =
            pdkpass_session_kind_from_name(name->valuestring);
        int64_t end_utc;
        if (kind >= PDKPASS_SESSION_COUNT ||
            !pdkpass_parse_iso8601_utc(date_end->valuestring, &end_utc) ||
            end_utc < window_start || end_utc > race.switch_at_utc) continue;

        session_cache_t *session = &cache->sessions[kind];
        int32_t session_key = (int32_t)key->valuedouble;
        update_session_identity(session, session_key,
                                json_bool(item, "is_cancelled"), end_utc);
        matched = true;
    }
    cJSON_Delete(root);
    if (matched) cache->discovered = 1;
    cache->last_discovery_utc = now_utc;
    return matched;
}

static bool parse_top_three(const char *body,
                            result_driver_t top[PDKPASS_PODIUM_SIZE])
{
    cJSON *root = cJSON_Parse(body);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    memset(top, 0, sizeof(result_driver_t) * PDKPASS_PODIUM_SIZE);
    const cJSON *item;
    cJSON_ArrayForEach(item, root) {
        const cJSON *position = cJSON_GetObjectItemCaseSensitive(item, "position");
        const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "driver_number");
        if (!cJSON_IsNumber(position) || !cJSON_IsNumber(number)) continue;
        int place = position->valueint;
        if (place >= 1 && place <= PDKPASS_PODIUM_SIZE) {
            top[place - 1].position = (unsigned)place;
            top[place - 1].driver_number = number->valueint;
        }
    }
    cJSON_Delete(root);
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        if (top[i].position != i + 1 || top[i].driver_number <= 0) return false;
    }
    return true;
}

static void copy_json_text(char *destination, size_t capacity,
                           const cJSON *object, const char *name,
                           const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    snprintf(destination, capacity, "%s",
             cJSON_IsString(item) ? item->valuestring : fallback);
}

static bool merge_driver_details(const char *body,
                                 const result_driver_t top[PDKPASS_PODIUM_SIZE],
                                 char podium_codes[PDKPASS_PODIUM_SIZE][4])
{
    cJSON *root = cJSON_Parse(body);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    memset(podium_codes, 0, PDKPASS_PODIUM_SIZE * 4U);
    const cJSON *item;
    cJSON_ArrayForEach(item, root) {
        const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "driver_number");
        if (!cJSON_IsNumber(number)) continue;
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            if (number->valueint != top[i].driver_number) continue;
            char fallback[8];
            snprintf(fallback, sizeof(fallback), "#%d", top[i].driver_number);
            copy_json_text(podium_codes[i], sizeof(podium_codes[i]), item,
                           "name_acronym", fallback);
        }
    }
    cJSON_Delete(root);
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        if (podium_codes[i][0] == '\0') return false;
    }
    return true;
}

static bool fetch_result(session_cache_t *session)
{
    char url[192];
    snprintf(url, sizeof(url),
             "https://api.openf1.org/v1/session_result?session_key=%ld&position%%3C=3",
             (long)session->session_key);
    char *body = NULL;
    if (http_get_json(url, &body) != ESP_OK) return false;
    result_driver_t top[PDKPASS_PODIUM_SIZE];
    bool parsed = parse_top_three(body, top);
    free(body);
    if (!parsed) return false;

    snprintf(url, sizeof(url),
             "https://api.openf1.org/v1/drivers?session_key=%ld",
             (long)session->session_key);
    if (http_get_json(url, &body) != ESP_OK) return false;
    parsed = merge_driver_details(body, top, session->podium_codes);
    free(body);
    if (parsed) session->ready = 1;
    return parsed;
}

static int64_t retry_interval_seconds(size_t race_index, int64_t now_utc)
{
    pdkpass_race_t race;
    if (!pdkpass_season_race_get(race_index, &race)) {
        return RESULTS_IDLE_DELAY_MS / 1000U;
    }
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

static size_t select_race(int64_t now_utc)
{
    size_t race_count = pdkpass_season_race_count();
    size_t requested = SIZE_MAX;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        requested = s_requested_race;
        s_requested_race = SIZE_MAX;
        xSemaphoreGive(s_lock);
    }
    if (requested < race_count &&
        race_needs_work(requested, now_utc)) return requested;
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
            ESP_LOGI(TAG, "R%u %s podium cached", race.round,
                     pdkpass_session_label((pdkpass_session_kind_t)i));
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
        esp_err_t err = save_cache();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "R%u cache save failed: %s",
                     race.round, esp_err_to_name(err));
        }
        if (err == ESP_OK) s_cache_dirty = false;
    }
    if (changed && s_callback) s_callback(race_index);
    return outcome;
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
    TickType_t delay = pdMS_TO_TICKS(RESULTS_IDLE_DELAY_MS);
    for (;;) {
        xEventGroupWaitBits(s_events, EVENT_WAKE, pdTRUE, pdFALSE, delay);
        if (!online_snapshot()) {
            delay = portMAX_DELAY;
            continue;
        }

        pdkpass_http_begin();
        int64_t now_utc = (int64_t)time(NULL);
        if (s_cache_dirty && save_cache() == ESP_OK) s_cache_dirty = false;
        size_t race_index = select_race(now_utc);
        size_t race_count = pdkpass_season_race_count();
        if (race_index >= race_count) {
            delay = next_scheduled_wait(now_utc);
            if (s_cache_dirty && delay > pdMS_TO_TICKS(60000)) delay = pdMS_TO_TICKS(60000);
            pdkpass_http_end();
            continue;
        }

        process_race(race_index, now_utc);
        delay = next_scheduled_wait((int64_t)time(NULL));
        if (delay < pdMS_TO_TICKS(RESULTS_BACKFILL_DELAY_MS)) {
            delay = pdMS_TO_TICKS(RESULTS_BACKFILL_DELAY_MS);
        }
        if (s_cache_dirty && delay > pdMS_TO_TICKS(60000)) delay = pdMS_TO_TICKS(60000);
        pdkpass_http_end();
    }
}

esp_err_t pdkpass_results_start(pdkpass_results_callback_t callback)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    if (pdkpass_http_init() != ESP_OK) return ESP_ERR_NO_MEM;
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) return ESP_ERR_NO_MEM;
    s_callback = callback;
    load_cache();
    if (xTaskCreate(results_task, "pdk_results", RESULTS_TASK_STACK, NULL,
                    RESULTS_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_results_set_online(bool online)
{
    if (!s_lock || !s_events) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_online = online;
        xSemaphoreGive(s_lock);
    }
    xEventGroupSetBits(s_events, EVENT_WAKE);
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
    if (!same) load_cache();
    xEventGroupSetBits(s_events, EVENT_WAKE);
}

void pdkpass_results_request_race(size_t race_index)
{
    if (!s_lock || !s_events ||
        race_index >= pdkpass_season_race_count()) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_requested_race = race_index;
        xSemaphoreGive(s_lock);
    }
    xEventGroupSetBits(s_events, EVENT_WAKE);
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
