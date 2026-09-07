#include "pdkpass_season.h"

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
#include "pdkpass_results_core.h"
#include "pdkpass_season_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEASON_TASK_STACK 9216
#define SEASON_TASK_PRIORITY 3
#define SEASON_BODY_LIMIT 24576U
#define SEASON_CACHE_MAGIC 0x50444B53U
#define SEASON_CACHE_VERSION 1U
#define SEASON_MIN_REPEAT_SECONDS (5LL * 60LL)

#define EVENT_WAKE BIT0

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    pdkpass_season_snapshot_t season;
} season_cache_t;



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
static pdkpass_season_callback_t s_callback;
static bool s_online;
static bool s_time_valid;
static int64_t s_last_attempt_utc;

_Static_assert(sizeof(season_cache_t) <= 6144,
               "Season snapshot no longer fits the NVS budget");

static esp_err_t http_get_json(const char *url, char **json)
{
    return pdkpass_http_get(url, SEASON_BODY_LIMIT, json);
}

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

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool json_number(const cJSON *object, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !value) return false;
    *value = item->valueint;
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

static void initialize_fallback(void)
{
    memset(&s_season, 0, sizeof(s_season));
    s_season.year = 2026;
    s_season.race_count = pdkpass_race_count > PDKPASS_MAX_RACES
                              ? PDKPASS_MAX_RACES
                              : (uint8_t)pdkpass_race_count;
    s_season.driver_count = pdkpass_driver_count > PDKPASS_MAX_DRIVERS
                                ? PDKPASS_MAX_DRIVERS
                                : (uint8_t)pdkpass_driver_count;
    copy_text(s_season.standings_as_of, sizeof(s_season.standings_as_of),
              "31 AUG");
    memcpy(s_season.races, pdkpass_races,
           s_season.race_count * sizeof(s_season.races[0]));
    memcpy(s_season.drivers, pdkpass_drivers,
           s_season.driver_count * sizeof(s_season.drivers[0]));
}

static bool snapshot_valid(const pdkpass_season_snapshot_t *season)
{
    if (!season || season->year < 2026U || season->year > 2100U ||
        season->race_count == 0U || season->race_count > PDKPASS_MAX_RACES ||
        season->driver_count > PDKPASS_MAX_DRIVERS) return false;
    for (size_t i = 0; i < season->race_count; i++) {
        if (season->races[i].round == 0U ||
            (i > 0U && season->races[i - 1U].switch_at_utc >=
                           season->races[i].switch_at_utc)) return false;
    }
    return true;
}

static void load_cache(void)
{
    initialize_fallback();
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    season_cache_t *stored = malloc(sizeof(*stored));
    if (!stored) {
        nvs_close(handle);
        return;
    }
    size_t size = sizeof(*stored);
    esp_err_t err = nvs_get_blob(handle, NVS_KEY, stored, &size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(*stored) &&
        stored->magic == SEASON_CACHE_MAGIC &&
        stored->version == SEASON_CACHE_VERSION &&
        snapshot_valid(&stored->season)) {
        s_season = stored->season;
        ESP_LOGI(TAG, "Loaded %u season: %u races, %u drivers",
                 s_season.year, s_season.race_count, s_season.driver_count);
    }
    free(stored);
}

static esp_err_t save_cache(const pdkpass_season_snapshot_t *season)
{
    season_cache_t *stored = calloc(1, sizeof(*stored));
    if (!stored) return ESP_ERR_NO_MEM;
    stored->magic = SEASON_CACHE_MAGIC;
    stored->version = SEASON_CACHE_VERSION;
    stored->season = *season;
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
    int64_t now_utc;
    int latest_race_session;
    int64_t latest_race_end;
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

    race_build_t *entry = &context->build[context->count++];
    memset(entry, 0, sizeof(*entry));
    entry->start_utc = start_utc;
    entry->meeting_end_utc = end_utc;
    entry->race.meeting_key = meeting_key;
    entry->race.switch_at_utc = end_utc;
    entry->race.accent = accent_for_key(circuit_key);
    copy_upper(entry->race.country, sizeof(entry->race.country), country);
    copy_upper(entry->race.circuit, sizeof(entry->race.circuit), circuit);
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
        if (end_utc <= context->now_utc - PDKPASS_RESULT_DELAY_SECONDS &&
            end_utc > context->latest_race_end) {
            context->latest_race_end = end_utc;
            context->latest_race_session = session_key;
        }
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
        for (size_t j = 0; j < current->race_count; j++) {
            const pdkpass_race_t *known = &current->races[j];
            pdkpass_race_t *race = &candidate->races[i];
            // Country is not unique within a season (for example Miami,
            // Austin, and Las Vegas), so only an exact circuit-name match may
            // inherit length/lap metadata from the fallback snapshot.
            if (!same_text(race->circuit, known->circuit)) continue;
            race->circuit_length_m = known->circuit_length_m;
            race->laps = known->laps;
            race->accent = known->accent;
            break;
        }
    }
}

static const cJSON *find_driver_number(const cJSON *drivers, int number)
{
    const cJSON *driver;
    cJSON_ArrayForEach(driver, drivers) {
        int candidate;
        if (json_number(driver, "driver_number", &candidate) &&
            candidate == number) return driver;
    }
    return NULL;
}

static uint32_t parse_colour(const char *text)
{
    if (!text || strlen(text) != 6U) return 0x3671C6;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 16);
    return end && *end == '\0' ? (uint32_t)value : 0x3671C6;
}

static int compare_drivers(const void *left, const void *right)
{
    const pdkpass_driver_t *a = left;
    const pdkpass_driver_t *b = right;
    return a->position < b->position ? -1
           : a->position > b->position ? 1
                                       : 0;
}

static bool fetch_standings(int session_key, int64_t now_utc,
                            pdkpass_season_snapshot_t *candidate)
{
    if (session_key <= 0) return false;
    char url[192];
    char *drivers_body = NULL;
    char *standings_body = NULL;
    snprintf(url, sizeof(url),
             "https://api.openf1.org/v1/drivers?session_key=%d", session_key);
    if (http_get_json(url, &drivers_body) != ESP_OK) return false;
    snprintf(url, sizeof(url),
             "https://api.openf1.org/v1/championship_drivers?session_key=%d",
             session_key);
    if (http_get_json(url, &standings_body) != ESP_OK) {
        free(drivers_body);
        return false;
    }

    cJSON *drivers = cJSON_Parse(drivers_body);
    cJSON *standings = cJSON_Parse(standings_body);
    free(drivers_body);
    free(standings_body);
    if (!cJSON_IsArray(drivers) || !cJSON_IsArray(standings)) {
        cJSON_Delete(drivers);
        cJSON_Delete(standings);
        return false;
    }

    pdkpass_driver_t parsed[PDKPASS_MAX_DRIVERS] = {0};
    size_t count = 0;
    const cJSON *standing;
    cJSON_ArrayForEach(standing, standings) {
        if (count >= PDKPASS_MAX_DRIVERS) break;
        int number;
        int position;
        const cJSON *points =
            cJSON_GetObjectItemCaseSensitive(standing, "points_current");
        if (!json_number(standing, "driver_number", &number) ||
            !json_number(standing, "position_current", &position) ||
            !cJSON_IsNumber(points) || number < 0 || number > 255 ||
            position <= 0 || position > 255) continue;

        pdkpass_driver_t *output = &parsed[count++];
        output->driver_number = (uint8_t)number;
        output->position = (uint8_t)position;
        double tenths = points->valuedouble * 10.0;
        if (tenths < 0.0) tenths = 0.0;
        output->points_tenths =
            tenths > 65535.0 ? 65535U : (uint16_t)(tenths + 0.5);
        const cJSON *driver = find_driver_number(drivers, number);
        if (driver) {
            copy_upper(output->code, sizeof(output->code),
                       json_string(driver, "name_acronym"));
            const char *last_name = json_string(driver, "last_name");
            copy_upper(output->name, sizeof(output->name),
                       last_name ? last_name : json_string(driver, "full_name"));
            copy_upper(output->team, sizeof(output->team),
                       json_string(driver, "team_name"));
            output->accent = parse_colour(json_string(driver, "team_colour"));
        }
        if (output->code[0] == '\0') {
            snprintf(output->code, sizeof(output->code), "%03u",
                     (unsigned)output->driver_number);
        }
        if (output->name[0] == '\0') copy_text(output->name, sizeof(output->name), output->code);
        if (output->team[0] == '\0') copy_text(output->team, sizeof(output->team), "TEAM");
    }
    cJSON_Delete(drivers);
    cJSON_Delete(standings);
    if (count == 0U) return false;
    qsort(parsed, count, sizeof(parsed[0]), compare_drivers);
    candidate->driver_count = (uint8_t)count;
    memcpy(candidate->drivers, parsed, count * sizeof(parsed[0]));
    pdkpass_format_beijing_date(now_utc, candidate->standings_as_of,
                                sizeof(candidate->standings_as_of));
    return true;
}

static bool build_candidate(unsigned year, int64_t now_utc,
                            const pdkpass_season_snapshot_t *current,
                            pdkpass_season_snapshot_t *candidate,
                            bool *retry)
{
    char url[128];
    race_build_t *build = calloc(PDKPASS_MAX_RACES, sizeof(*build));
    if (!build) return false;
    build_context_t context = {.build = build, .now_utc = now_utc};
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
    if (context.latest_race_session > 0 &&
        !fetch_standings(context.latest_race_session, context.latest_race_end, candidate)) {
        *retry = true;
    }
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
    bool retry = false;
    bool success = build_candidate(target_year, now_utc, current, candidate, &retry);
    bool updated = success && memcmp(current, candidate, sizeof(*candidate)) != 0;
    if (updated) {
        esp_err_t err = save_cache(candidate);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Season cache save failed: %s", esp_err_to_name(err));
            updated = false;
            success = false;
        } else if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
            s_season = *candidate;
            xSemaphoreGive(s_lock);
        } else {
            updated = false;
            success = false;
        }
    }
    if (updated) {
        ESP_LOGI(TAG, "Adopted %u season: %u races, %u drivers", candidate->year,
                 candidate->race_count, candidate->driver_count);
        if (s_callback) s_callback();
    }
    free(current);
    free(candidate);
    return success && !retry;
}

static void season_task(void *arg)
{
    (void)arg;
    TickType_t delay = portMAX_DELAY;
    for (;;) {
        xEventGroupWaitBits(s_events, EVENT_WAKE, pdTRUE, pdFALSE, delay);
        if (!network_ready()) {
            delay = portMAX_DELAY;
            continue;
        }
        int64_t now_utc = (int64_t)time(NULL);
        if (s_last_attempt_utc != 0 && now_utc >= s_last_attempt_utc &&
            now_utc - s_last_attempt_utc < SEASON_MIN_REPEAT_SECONDS) {
            delay = pdMS_TO_TICKS((uint32_t)(s_last_attempt_utc + SEASON_MIN_REPEAT_SECONDS - now_utc) * 1000U);
            continue;
        }
        pdkpass_http_begin();
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
    }
}

esp_err_t pdkpass_season_start(pdkpass_season_callback_t callback)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    if (pdkpass_http_init() != ESP_OK) return ESP_ERR_NO_MEM;
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) return ESP_ERR_NO_MEM;
    s_callback = callback;
    load_cache();
    if (xTaskCreate(season_task, "pdk_season", SEASON_TASK_STACK, NULL,
                    SEASON_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pdkpass_season_set_network(bool online, bool time_valid)
{
    if (!s_lock || !s_events) return;
    bool wake = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        wake = (online && time_valid) && (!s_online || !s_time_valid);
        s_online = online;
        s_time_valid = time_valid;
        xSemaphoreGive(s_lock);
    }
    if (wake) xEventGroupSetBits(s_events, EVENT_WAKE);
}

bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *snapshot)
{
    if (!snapshot || !s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    *snapshot = s_season;
    xSemaphoreGive(s_lock);
    return true;
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
