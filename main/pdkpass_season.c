#include "pdkpass_season.h"
#include "pdkpass_calendar.h"
#include "pdkpass_tracks.h"
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
#include "pdkpass_results_core.h"
#include "pdkpass_season_core.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEASON_TASK_STACK 9216
#define SEASON_TASK_PRIORITY 3
#define SEASON_CACHE_MAGIC 0x50444B53U
#define SEASON_CACHE_VERSION 2U
#define SEASON_CACHE_LEGACY_VERSION 1U
#define SEASON_MIN_REPEAT_SECONDS (5LL * 60LL)
#define POINTS_FORCE_COOLDOWN_MS 60000U

#define EVENT_WAKE BIT0

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    pdkpass_season_snapshot_t season;
} season_cache_t;

typedef struct {
    uint32_t accent;
    uint16_t points_tenths;
    uint8_t position;
    uint8_t driver_number;
    char code[4];
    char name[PDKPASS_DRIVER_NAME_LEN];
    char team[PDKPASS_TEAM_LEN];
} legacy_driver_t;

typedef struct {
    uint16_t year;
    uint8_t race_count;
    uint8_t driver_count;
    char standings_as_of[12];
    pdkpass_race_t races[PDKPASS_MAX_RACES];
    legacy_driver_t drivers[PDKPASS_MAX_DRIVERS];
} legacy_season_snapshot_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    legacy_season_snapshot_t season;
} legacy_season_cache_t;



typedef struct {
    pdkpass_race_t race;
    int64_t start_utc;
    int64_t meeting_end_utc;
} race_build_t;

static const char *TAG = "pdkpass_season";
static const char *NVS_NAMESPACE = "pdk_season";
static const char *NVS_KEY = "current";
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static pdkpass_season_snapshot_t s_season;
static pdkpass_team_snapshot_t s_teams;
#define TEAM_CACHE_MAGIC 0x5044544DU
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    pdkpass_team_snapshot_t snapshot;
} team_cache_t;
_Static_assert(sizeof(team_cache_t) <= 1024, "Team cache exceeds RAM budget");
static bool s_has_cached_data;
static pdkpass_season_callback_t s_callback;
static bool s_online;
static bool s_time_valid;
static int64_t s_last_attempt_utc;
static bool s_points_force_pending;
static pdkpass_manual_status_t s_points_force_status;
static TickType_t s_points_force_last_tick;
static bool s_points_force_has_last_tick;

_Static_assert(sizeof(season_cache_t) <= 6144,
               "Season snapshot no longer fits the NVS budget");
_Static_assert(sizeof(legacy_driver_t) == 48,
               "Legacy driver cache layout changed");

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0U) return;
    snprintf(destination, capacity, "%s", source ? source : "");
}

static void copy_upper(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0U) return;
    size_t output = 0;
    if (source) {
        while (*source && output + 1U < capacity) {
            unsigned char value = (unsigned char)*source++;
            destination[output++] =
                value < 0x80U ? (char)toupper(value) : (char)value;
        }
    }
    destination[output] = '\0';
}

static void fill_known_first_name(pdkpass_driver_t *driver)
{
    if (!driver) return;
    // Retain the roster's familiar display name (for example KIMI) when the
    // API supplies a longer legal given name. Unknown drivers keep API data.
    for (size_t i = 0; i < pdkpass_legacy_driver_count; i++) {
        const pdkpass_driver_t *known = &pdkpass_legacy_drivers[i];
        if (strncmp(driver->code, known->code, sizeof(driver->code)) == 0 &&
            strncmp(driver->name, known->name, sizeof(driver->name)) == 0) {
            copy_text(driver->first_name, sizeof(driver->first_name),
                      known->first_name);
            return;
        }
    }
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool json_number(const cJSON *object, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !value || !(item->valuedouble >= 0) ||
        item->valuedouble > INT_MAX) return false;
    int number = (int)item->valuedouble;
    if (item->valuedouble != number) return false;
    *value = number;
    return true;
}

static uint32_t accent_for_key(int circuit_key)
{
    static const uint32_t palette[] = {
        0xE32636, 0xF2A900, 0x00A6C8, 0xFF7A00,
        0x8A3FFC, 0x0057B8, 0x00843D, 0xD3208B,
        0x00A9A5, 0x3671C6, 0x229971, 0xFF8700,
    };
    unsigned index = circuit_key >= 0 ? (unsigned)circuit_key
                                      : (unsigned)(-circuit_key);
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

static void apply_track_details(pdkpass_race_t *race, const char *name)
{
    const pdkpass_track_info_t *track = pdkpass_track_find(name);
    if (!track) return;
    copy_text(race->circuit, sizeof(race->circuit), track->name);
    race->circuit_length_m = track->length_m;
    race->accent = track->accent;
}

static void initialize_fallback(void)
{
    pdkpass_calendar_load(2026, &s_season);
}

#include "pdkpass_season_store.inc"

static int compare_races(const void *left, const void *right)
{
    const race_build_t *a = left;
    const race_build_t *b = right;
    return a->start_utc < b->start_utc ? -1
           : a->start_utc > b->start_utc ? 1
                                         : 0;
}

static bool is_grand_prix(const cJSON *meeting)
{
    const char *name = json_string(meeting, "meeting_name");
    const cJSON *cancelled =
        cJSON_GetObjectItemCaseSensitive(meeting, "is_cancelled");
    return name && strstr(name, "Grand Prix") != NULL &&
           !cJSON_IsTrue(cancelled);
}

typedef struct {
    race_build_t *build;
    size_t count;
    bool sprint_qualifying[PDKPASS_MAX_RACES];
} build_context_t;

static bool parse_meeting(const cJSON *meeting, void *user)
{
    build_context_t *context = user;
    if (!is_grand_prix(meeting)) return true;
    if (context->count >= PDKPASS_MAX_RACES) return false;
    int meeting_key;
    int circuit_key = 0;
    const char *start_text = json_string(meeting, "date_start");
    const char *end_text = json_string(meeting, "date_end");
    const char *country = json_string(meeting, "country_name");
    const char *circuit = json_string(meeting, "circuit_short_name");
    int64_t start_utc;
    int64_t end_utc;
    if (!json_number(meeting, "meeting_key", &meeting_key) ||
        !start_text || !end_text || !country || !circuit ||
        !pdkpass_parse_iso8601_utc(start_text, &start_utc) ||
        !pdkpass_parse_iso8601_utc(end_text, &end_utc) ||
        end_utc <= start_utc || meeting_key <= 0) return false;
    json_number(meeting, "circuit_key", &circuit_key);
    for (size_t i = 0; i < context->count; i++)
        if (context->build[i].race.meeting_key == meeting_key) return false;

    race_build_t *entry = &context->build[context->count++];
    memset(entry, 0, sizeof(*entry));
    entry->start_utc = start_utc;
    entry->meeting_end_utc = end_utc;
    entry->race.meeting_key = meeting_key;
    entry->race.switch_at_utc = end_utc;
    entry->race.accent = accent_for_key(circuit_key);
    copy_upper(entry->race.country, sizeof(entry->race.country), country);
    copy_upper(entry->race.circuit, sizeof(entry->race.circuit), circuit);
    // Resolve the original name before the short display buffer truncates it.
    apply_track_details(&entry->race, circuit);
    copy_text(entry->race.api_country, sizeof(entry->race.api_country),
              country);
    pdkpass_format_beijing_weekend(start_utc, end_utc,
                                   entry->race.weekend,
                                   sizeof(entry->race.weekend));
    copy_text(entry->race.session_one_cn,
              sizeof(entry->race.session_one_cn), "SCHEDULE PENDING");
    copy_text(entry->race.session_two_cn,
              sizeof(entry->race.session_two_cn), "SCHEDULE PENDING");
    copy_text(entry->race.race_cn, sizeof(entry->race.race_cn),
              "RACE SCHEDULE TBD");
    return true;
}

static race_build_t *find_meeting(race_build_t *build, size_t count,
                                  int meeting_key)
{
    for (size_t i = 0; i < count; i++) {
        if (build[i].race.meeting_key == meeting_key) return &build[i];
    }
    return NULL;
}

static const char *session_short_label(pdkpass_session_kind_t kind)
{
    static const char *labels[PDKPASS_SESSION_COUNT] = {
        "FP1", "FP2", "FP3", "SPR Q", "SPR", "QUALI", "RACE",
    };
    return kind < PDKPASS_SESSION_COUNT ? labels[kind] : "EVENT";
}

static bool populate_session(const cJSON *session, void *user)
{
    build_context_t *context = user;
    int meeting_key;
    int session_key;
    const char *name = json_string(session, "session_name");
    const char *start_text = json_string(session, "date_start");
    const char *end_text = json_string(session, "date_end");
    const cJSON *cancelled =
        cJSON_GetObjectItemCaseSensitive(session, "is_cancelled");
    int64_t start_utc;
    int64_t end_utc;
    if (cJSON_IsTrue(cancelled) || !name || !start_text || !end_text ||
        !json_number(session, "meeting_key", &meeting_key) ||
        !json_number(session, "session_key", &session_key) ||
        !pdkpass_parse_iso8601_utc(start_text, &start_utc) ||
        !pdkpass_parse_iso8601_utc(end_text, &end_utc)) return true;
    race_build_t *entry = find_meeting(context->build, context->count, meeting_key);
    if (!entry) return true;
    size_t index = (size_t)(entry - context->build);
    pdkpass_session_kind_t kind = pdkpass_session_kind_from_name(name);
    if (kind >= PDKPASS_SESSION_COUNT) return true;

    char line[PDKPASS_SESSION_LINE_LEN];
    pdkpass_format_beijing_session(session_short_label(kind), start_utc,
                                   line, sizeof(line));
    if (kind == PDKPASS_SESSION_FP1) {
        copy_text(entry->race.session_one_cn,
                  sizeof(entry->race.session_one_cn), line);
    } else if (kind == PDKPASS_SESSION_SPRINT_QUALIFYING) {
        context->sprint_qualifying[index] = true;
        copy_text(entry->race.session_two_cn,
                  sizeof(entry->race.session_two_cn), line);
    } else if (kind == PDKPASS_SESSION_QUALIFYING &&
               !context->sprint_qualifying[index]) {
        copy_text(entry->race.session_two_cn,
                  sizeof(entry->race.session_two_cn), line);
    } else if (kind == PDKPASS_SESSION_RACE) {
        copy_text(entry->race.race_cn, sizeof(entry->race.race_cn), line);
        entry->race.switch_at_utc = end_utc;

    }
    return true;
}

static bool same_text(const char *left, const char *right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a < 0x80U) a = (unsigned char)toupper(a);
        if (b < 0x80U) b = (unsigned char)toupper(b);
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

static void preserve_track_details(pdkpass_season_snapshot_t *candidate,
                                   const pdkpass_season_snapshot_t *current)
{
    for (size_t i = 0; i < candidate->race_count; i++) {
        pdkpass_race_t *race = &candidate->races[i];
        apply_track_details(race, race->circuit);
        // Race laps are event data: never inherit them from another season.
        if (candidate->year != current->year) continue;
        for (size_t j = 0; j < current->race_count; j++) {
            const pdkpass_race_t *known = &current->races[j];
            const pdkpass_track_info_t *a = pdkpass_track_find(race->circuit);
            const pdkpass_track_info_t *b = pdkpass_track_find(known->circuit);
            if (!(a && b ? a == b : same_text(race->circuit, known->circuit))) continue;
            if (race->meeting_key > 0 && known->meeting_key > 0 &&
                race->meeting_key != known->meeting_key) continue;
            race->laps = known->laps;
            break;
        }
    }
}

// Jolpica encodes numeric fields as decimal strings. Reject signs, trailing
// garbage, overflow and fractional values except the single points decimal.
#include "pdkpass_season_standings.inc"

static bool build_candidate(unsigned year,
                            const pdkpass_season_snapshot_t *current,
                            pdkpass_season_snapshot_t *candidate)
{
    char url[128];
    race_build_t *build = calloc(PDKPASS_MAX_RACES, sizeof(*build));
    if (!build) {
        pdkpass_http_report_data_failure("season-build-alloc", ESP_ERR_NO_MEM,
                                         PDKPASS_MAX_RACES * sizeof(*build));
        return false;
    }
    build_context_t context = {.build = build};
    snprintf(url, sizeof(url), "https://api.openf1.org/v1/meetings?year=%u", year);
    if (pdkpass_http_array(url, parse_meeting, &context) != ESP_OK ||
        !pdkpass_season_candidate_valid(current->year, year, context.count)) {
        free(build);
        return false;
    }
    qsort(build, context.count, sizeof(build[0]), compare_races);
    for (size_t i = 0; i < context.count; i++) {
        build[i].race.round = (uint8_t)(i + 1U);
        // A successful but incomplete sessions response must not erase known
        // times for the same meeting. An explicit cancellation is handled below.
        if (year != current->year) continue;
        for (size_t j = 0; j < current->race_count; j++) {
            const pdkpass_race_t *known = &current->races[j];
            if (known->meeting_key <= 0 || known->meeting_key != build[i].race.meeting_key) continue;
            copy_text(build[i].race.session_one_cn, sizeof(build[i].race.session_one_cn), known->session_one_cn);
            copy_text(build[i].race.session_two_cn, sizeof(build[i].race.session_two_cn), known->session_two_cn);
            copy_text(build[i].race.race_cn, sizeof(build[i].race.race_cn), known->race_cn);
            build[i].race.switch_at_utc = known->switch_at_utc;
            break;
        }
    }
    snprintf(url, sizeof(url), "https://api.openf1.org/v1/sessions?year=%u", year);
    if (pdkpass_http_array(url, populate_session, &context) != ESP_OK) {
        free(build);
        return false;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->year = (uint16_t)year;
    candidate->race_count = (uint8_t)context.count;
    if (year == current->year) {
        candidate->driver_count = current->driver_count;
        memcpy(candidate->drivers, current->drivers, sizeof(candidate->drivers));
        copy_text(candidate->standings_as_of, sizeof(candidate->standings_as_of), current->standings_as_of);
    } else {
        copy_text(candidate->standings_as_of, sizeof(candidate->standings_as_of), "PENDING");
    }
    for (size_t i = 0; i < context.count; i++) candidate->races[i] = build[i].race;
    free(build);
    preserve_track_details(candidate, current);
    return snapshot_valid(candidate);
}

static bool network_ready(void)
{
    bool ready = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ready = s_online && s_time_valid;
        xSemaphoreGive(s_lock);
    }
    return ready;
}

static int64_t next_sync_deadline(int64_t now_utc)
{
    int64_t deadline = pdkpass_next_beijing_midnight(now_utc);
    size_t count = pdkpass_season_race_count();
    for (size_t i = 0; i < count; i++) {
        pdkpass_race_t race;
        if (!pdkpass_season_race_get(i, &race)) continue;
        int64_t next = pdkpass_season_next_race_check(now_utc, race.switch_at_utc);
        if (next < deadline) deadline = next;
    }
    return deadline;
}

static bool synchronize(int64_t now_utc)
{
    pdkpass_season_snapshot_t *current = malloc(sizeof(*current));
    pdkpass_season_snapshot_t *candidate = malloc(sizeof(*candidate));
    if (!current || !candidate) {
        pdkpass_http_report_data_failure("season-snapshot-alloc", ESP_ERR_NO_MEM,
                                         sizeof(*current) + sizeof(*candidate));
        free(current);
        free(candidate);
        return false;
    }
    if (!pdkpass_season_snapshot(current)) {
        free(current);
        free(candidate);
        return false;
    }
    unsigned target_year = pdkpass_beijing_year(now_utc);
    bool calendar_ok = build_candidate(target_year, current, candidate);
    if (!calendar_ok) *candidate = *current;
    // Standings have their own source. A failed OpenF1 calendar request must
    // never prevent same-season Jolpica updates or discard the cached calendar.
    bool standings_ok = candidate->year == target_year &&
                        fetch_standings(now_utc, candidate);
    if (!standings_ok) ESP_LOGW(TAG, "Jolpica standings unchanged; retry scheduled");
    bool success = calendar_ok && standings_ok;
    bool updated = (calendar_ok || standings_ok) &&
                   memcmp(current, candidate, sizeof(*candidate)) != 0;
    if (updated) {
        esp_err_t err = save_cache(candidate);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Season cache save failed: %s", esp_err_to_name(err));
            updated = false;
            success = calendar_ok = standings_ok = false;
        } else if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
            s_season = *candidate;
            s_has_cached_data = true;
            xSemaphoreGive(s_lock);
        } else {
            updated = false;
            success = calendar_ok = standings_ok = false;
        }
    }
    if (updated) {
        if (s_callback) s_callback();
    }
    if (calendar_ok)
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_CALENDAR, (int64_t)time(NULL));
    if (standings_ok)
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_DRIVERS, (int64_t)time(NULL));
    free(current);
    free(candidate);
    bool teams_ok = synchronize_teams(target_year, now_utc);
    return success && teams_ok;
}

static pdkpass_manual_state_t synchronize_manual_points(int64_t now_utc)
{
    unsigned year = pdkpass_beijing_year(now_utc);
    pdkpass_season_snapshot_t *candidate = malloc(sizeof(*candidate));
    bool drivers_ok = candidate && pdkpass_season_snapshot(candidate) &&
                      candidate->year == year;
    bool drivers_changed = false;
    if (drivers_ok) {
        drivers_ok = fetch_standings(now_utc, candidate) && !pdkpass_http_expired();
        bool changed = false;
        if (drivers_ok && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            changed = memcmp(&s_season, candidate, sizeof(*candidate)) != 0;
            xSemaphoreGive(s_lock);
        } else drivers_ok = false;
        if (drivers_ok && changed) {
            drivers_ok = save_cache(candidate) == ESP_OK;
            if (drivers_ok && xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
                s_season = *candidate;
                s_has_cached_data = true;
                xSemaphoreGive(s_lock);
                drivers_changed = true;
                if (s_callback) s_callback();
            } else drivers_ok = false;
        }
    }
    if (drivers_ok && !pdkpass_http_expired())
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_DRIVERS, (int64_t)time(NULL));
    free(candidate);

    pdkpass_team_snapshot_t teams_before;
    pdkpass_team_snapshot_t teams_after;
    bool teams_ok = pdkpass_season_team_snapshot(&teams_before) &&
                    teams_before.year == year;
    bool teams_changed = false;
    if (teams_ok) {
        teams_after = teams_before;
        teams_ok = fetch_team_standings(now_utc, &teams_after) && !pdkpass_http_expired();
        if (teams_ok && memcmp(&teams_before, &teams_after, sizeof(teams_after)) != 0) {
            teams_ok = save_team_cache(&teams_after) == ESP_OK;
            if (teams_ok && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_teams = teams_after;
                xSemaphoreGive(s_lock);
                teams_changed = true;
                if (s_callback) s_callback();
            } else teams_ok = false;
        }
    }
    if (teams_ok && !pdkpass_http_expired())
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_TEAMS, (int64_t)time(NULL));
    if (pdkpass_http_expired()) return PDKPASS_MANUAL_TIMED_OUT;
    if (!drivers_ok && !teams_ok) return PDKPASS_MANUAL_FAILED;
    if (!drivers_ok || !teams_ok) return PDKPASS_MANUAL_PARTIAL;
    return drivers_changed || teams_changed ? PDKPASS_MANUAL_UPDATED :
                                         PDKPASS_MANUAL_UNCHANGED;
}

static bool take_points_force(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool pending = s_points_force_pending;
    s_points_force_pending = false;
    xSemaphoreGive(s_lock);
    return pending;
}

static bool points_force_pending(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool pending = s_points_force_pending;
    xSemaphoreGive(s_lock);
    return pending;
}

static void finish_points_force(pdkpass_manual_state_t state)
{
    // Worker only; terminal status must not be dropped on lock contention.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_points_force_status.state == PDKPASS_MANUAL_RUNNING) {
        s_points_force_status.state = pdkpass_manual_visible_status(
            s_points_force_status, esp_timer_get_time()).state == PDKPASS_MANUAL_TIMED_OUT
                ? PDKPASS_MANUAL_TIMED_OUT : state;
        s_points_force_status.generation++;
    }
    xSemaphoreGive(s_lock);
}

static int64_t manual_deadline(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t deadline = s_points_force_status.deadline_us;
    xSemaphoreGive(s_lock);
    return deadline;
}

static bool clock_valid(void)
{
    bool valid = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        valid = s_time_valid;
        xSemaphoreGive(s_lock);
    }
    return valid;
}

static void refresh_builtin_year(int64_t now_utc)
{
    if (!clock_valid()) return;
    unsigned year = pdkpass_beijing_year(now_utc);
    if (!pdkpass_calendar_supported(year) || year <= pdkpass_season_year()) return;
    // Serialize the season/result cache transition with existing HTTP work.
    // Fill the existing snapshot in place: no extra season-sized allocation.
    pdkpass_http_begin();
    bool changed = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        if (s_time_valid && year > s_season.year) {
            changed = pdkpass_calendar_load(year, &s_season);
            if (changed) s_has_cached_data = false;
        }
        xSemaphoreGive(s_lock);
    }
    if (changed) {
        s_last_attempt_utc = 0;
        pdkpass_sync_plan(PDKPASS_SYNC_SEASON, 0);
        if (s_callback) s_callback();
    }
    pdkpass_http_end();
}

static TickType_t offline_wait(uint32_t network_wait_ms, int64_t now_utc)
{
    // Offline is not an indefinite wait when a valid clock can cross New Year.
    uint32_t wait = network_wait_ms ? network_wait_ms : UINT32_MAX;
    if (clock_valid()) {
        uint32_t midnight_ms = (uint32_t)(pdkpass_next_beijing_midnight(now_utc) - now_utc) * 1000U;
        if (midnight_ms < wait) wait = midnight_ms;
    }
    return wait == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(wait);
}

static void season_task(void *arg)
{
    (void)arg;
    TickType_t delay = portMAX_DELAY;
    for (;;) {
        xEventGroupWaitBits(s_events, EVENT_WAKE, pdTRUE, pdFALSE, delay);
        refresh_builtin_year((int64_t)time(NULL));
        uint32_t wait_ms = points_force_pending() ? 0 :
                           pdkpass_sync_wait_ms(PDKPASS_SYNC_SEASON);
        if (!network_ready()) {
            if (take_points_force()) finish_points_force(PDKPASS_MANUAL_OFFLINE);
            if (!wait_ms) pdkpass_network_request(PDKPASS_NETWORK_SYNC);
            delay = offline_wait(wait_ms, (int64_t)time(NULL));
            continue;
        }
        if (wait_ms) { delay = pdMS_TO_TICKS(wait_ms); continue; }
        int64_t now_utc = (int64_t)time(NULL);
        if (take_points_force()) {
            if (pdkpass_http_begin_until(manual_deadline())) {
                pdkpass_manual_state_t state = network_ready()
                    ? synchronize_manual_points((int64_t)time(NULL)) : PDKPASS_MANUAL_OFFLINE;
                finish_points_force(state);
                pdkpass_http_end();
            } else finish_points_force(PDKPASS_MANUAL_TIMED_OUT);
            uint32_t resume_ms = pdkpass_sync_wait_ms(PDKPASS_SYNC_SEASON);
            delay = resume_ms ? pdMS_TO_TICKS(resume_ms) : 1;
            continue;
        }
        if (s_last_attempt_utc != 0 && now_utc >= s_last_attempt_utc &&
            now_utc - s_last_attempt_utc < SEASON_MIN_REPEAT_SECONDS) {
            delay = pdMS_TO_TICKS((uint32_t)(s_last_attempt_utc + SEASON_MIN_REPEAT_SECONDS - now_utc) * 1000U);
            pdkpass_sync_plan(PDKPASS_SYNC_SEASON, delay * portTICK_PERIOD_MS);
            continue;
        }
        pdkpass_http_begin();
        if (!network_ready()) { pdkpass_http_end(); delay = 1; continue; }
        now_utc = (int64_t)time(NULL);
        s_last_attempt_utc = now_utc;
        bool success = synchronize(now_utc);
        pdkpass_http_end();
        int64_t finished = (int64_t)time(NULL);
        // Compute from the attempt start so a due boundary crossed while HTTP
        // was active is not silently skipped.
        int64_t deadline = next_sync_deadline(now_utc);
        if (!success && deadline > finished + SEASON_MIN_REPEAT_SECONDS) {
            deadline = finished + SEASON_MIN_REPEAT_SECONDS;
        }
        int64_t seconds = deadline > finished ? deadline - finished : 1LL;
        delay = pdMS_TO_TICKS((uint32_t)seconds * 1000U);
        pdkpass_sync_plan(PDKPASS_SYNC_SEASON, (uint32_t)seconds * 1000U);
    }
}

esp_err_t pdkpass_season_start(pdkpass_season_callback_t callback)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    if (pdkpass_http_init() != ESP_OK) return ESP_ERR_NO_MEM;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) {
        if (s_events) vEventGroupDelete(s_events);
        s_events = NULL;
        pdkpass_http_report_data_failure("season-startup", ESP_ERR_NO_MEM, 0);
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    load_cache();
    load_team_cache();
    if (xTaskCreate(season_task, "pdk_season", SEASON_TASK_STACK, NULL,
                    SEASON_TASK_PRIORITY, NULL) != pdPASS) {
        // Preserve readable caches, but never accept work without a worker.
        vEventGroupDelete(s_events);
        s_events = NULL;
        pdkpass_http_report_data_failure("season-task", ESP_ERR_NO_MEM, SEASON_TASK_STACK);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_season_set_network(bool online, bool time_valid)
{
    if (!s_lock || !s_events) return;
    bool wake = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        wake = time_valid && (!s_time_valid || (online && !s_online));
        s_online = online;
        s_time_valid = time_valid;
        xSemaphoreGive(s_lock);
    }
    if (wake) xEventGroupSetBits(s_events, EVENT_WAKE);
}

pdkpass_manual_state_t pdkpass_season_force_points(void)
{
    if (!s_lock || !s_events || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return PDKPASS_MANUAL_BUSY;
    TickType_t now = xTaskGetTickCount();
    pdkpass_manual_state_t result = PDKPASS_MANUAL_RUNNING;
    if (!s_online || !s_time_valid) result = PDKPASS_MANUAL_OFFLINE;
    else if (s_points_force_status.state == PDKPASS_MANUAL_RUNNING)
        result = PDKPASS_MANUAL_BUSY;
    else if (s_points_force_has_last_tick &&
             now - s_points_force_last_tick < pdMS_TO_TICKS(POINTS_FORCE_COOLDOWN_MS))
        result = PDKPASS_MANUAL_COOLDOWN;
    else {
        s_points_force_has_last_tick = true;
        s_points_force_last_tick = now;
        s_points_force_pending = true;
        s_points_force_status.cooldown_until_us = esp_timer_get_time() + 60000000LL;
        s_points_force_status.deadline_us = esp_timer_get_time() + PDKPASS_MANUAL_TIMEOUT_US;
        s_points_force_status.state = PDKPASS_MANUAL_RUNNING;
        s_points_force_status.generation++;
    }
    xSemaphoreGive(s_lock);
    if (result == PDKPASS_MANUAL_RUNNING) {
        xEventGroupSetBits(s_events, EVENT_WAKE);
    }
    return result;
}

bool pdkpass_season_manual_status(pdkpass_manual_status_t *status)
{
    if (!status || !s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return false;
    *status = pdkpass_manual_visible_status(s_points_force_status, esp_timer_get_time());
    xSemaphoreGive(s_lock);
    return true;
}

bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *snapshot)
{
    if (!snapshot || !s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    *snapshot = s_season;
    xSemaphoreGive(s_lock);
    return true;
}

bool pdkpass_season_has_cached_data(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool cached = s_has_cached_data;
    xSemaphoreGive(s_lock);
    return cached;
}

unsigned pdkpass_season_year(void)
{
    unsigned year = 0;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        year = s_season.year;
        xSemaphoreGive(s_lock);
    }
    return year;
}

size_t pdkpass_season_race_count(void)
{
    size_t count = 0;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        count = s_season.race_count;
        xSemaphoreGive(s_lock);
    }
    return count;
}

size_t pdkpass_season_driver_count(void)
{
    size_t count = 0;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        count = s_season.driver_count;
        xSemaphoreGive(s_lock);
    }
    return count;
}

bool pdkpass_season_race_get(size_t index, pdkpass_race_t *race)
{
    if (!race || !s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool available = index < s_season.race_count;
    if (available) *race = s_season.races[index];
    xSemaphoreGive(s_lock);
    return available;
}

bool pdkpass_season_driver_get(size_t index, pdkpass_driver_t *driver)
{
    if (!driver || !s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool available = index < s_season.driver_count;
    if (available) *driver = s_season.drivers[index];
    xSemaphoreGive(s_lock);
    return available;
}

bool pdkpass_season_driver_by_code(const char *code,
                                   pdkpass_driver_t *driver)
{
    if (!code || !driver || !s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool found = false;
    for (size_t i = 0; i < s_season.driver_count; i++) {
        if (strncmp(s_season.drivers[i].code, code,
                    sizeof(s_season.drivers[i].code)) == 0) {
            *driver = s_season.drivers[i];
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}
