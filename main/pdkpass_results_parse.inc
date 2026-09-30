// Private implementation: included by the owning service, with its existing lock and worker.
static bool json_positive_key(const cJSON *item, int32_t *out)
{
    if (!cJSON_IsNumber(item) || !(item->valuedouble > 0) ||
        item->valuedouble > INT32_MAX) return false;
    int32_t value = (int32_t)item->valuedouble;
    if (item->valuedouble != value) return false;
    *out = value;
    return true;
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

typedef struct {
    race_cache_t *cache;
    int64_t window_start, window_end;
    bool matched;
    bool valid;
    int32_t keys[PDKPASS_SESSION_COUNT];
    bool reminder_valid, reminder_matched;
    int32_t meeting_key, reminder_meeting;
    pdkpass_reminder_entry_t reminders[PDKPASS_SESSION_COUNT];
} session_discovery_t;

static void parse_reminder_session(const cJSON *item, session_discovery_t *d)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "session_name");
    if (!cJSON_IsString(name)) { d->reminder_valid = false; return; }
    pdkpass_session_kind_t kind = pdkpass_session_kind_from_name(name->valuestring);
    if (kind >= PDKPASS_SESSION_COUNT) return;
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(item, "session_key");
    const cJSON *meeting = cJSON_GetObjectItemCaseSensitive(item, "meeting_key");
    if (d->meeting_key > 0 && cJSON_IsNumber(meeting) && meeting->valueint != d->meeting_key)
        return;
    const cJSON *start = cJSON_GetObjectItemCaseSensitive(item, "date_start");
    const cJSON *end = cJSON_GetObjectItemCaseSensitive(item, "date_end");
    int64_t start_utc = 0, end_utc = 0;
    bool has_start = cJSON_IsString(start) && pdkpass_parse_iso8601_utc(start->valuestring, &start_utc);
    bool has_end = cJSON_IsString(end) && pdkpass_parse_iso8601_utc(end->valuestring, &end_utc);
    if ((has_start && (start_utc < d->window_start || start_utc > d->window_end)) ||
        (has_end && (end_utc < d->window_start || end_utc > d->window_end))) return;
    bool cancelled = json_bool(item, "is_cancelled");
    if (!cJSON_IsNumber(key) || key->valueint <= 0 || d->reminders[kind].session_key ||
        (!cancelled && (!has_start || !has_end || start_utc >= end_utc)) ||
        (!has_start && !has_end && d->meeting_key <= 0)) {
        d->reminder_valid = false;
        return;
    }
    int32_t reminder_meeting = cJSON_IsNumber(meeting) ? meeting->valueint : d->meeting_key;
    if (reminder_meeting <= 0 || (d->reminder_meeting && d->reminder_meeting != reminder_meeting)) {
        d->reminder_valid = false;
        return;
    }
    d->reminder_meeting = reminder_meeting;
    d->reminders[kind] = (pdkpass_reminder_entry_t){
        .session_key = key->valueint, .start_utc = start_utc,
        .flags = cancelled ? PDKPASS_REMINDER_CANCELLED : 0,
    };
    d->reminder_matched = true;
}

static bool parse_discovered_session(const cJSON *item, void *user)
{
    session_discovery_t *d = user;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "session_name");
    if (!cJSON_IsString(name)) { d->valid = false; return false; }
    pdkpass_session_kind_t kind = pdkpass_session_kind_from_name(name->valuestring);
    if (kind >= PDKPASS_SESSION_COUNT) return true;
    int32_t key, meeting;
    if (!json_positive_key(cJSON_GetObjectItemCaseSensitive(item, "session_key"), &key) ||
        !json_positive_key(cJSON_GetObjectItemCaseSensitive(item, "meeting_key"), &meeting)) {
        d->valid = false; return false;
    }
    const cJSON *date_end = cJSON_GetObjectItemCaseSensitive(item, "date_end");
    bool cancelled = json_bool(item, "is_cancelled");
    int64_t end_utc = 0;
    if (!cJSON_IsString(date_end) || !pdkpass_parse_iso8601_utc(date_end->valuestring, &end_utc)) {
        if (!cancelled || d->meeting_key <= 0) { d->valid = false; return false; }
    } else if (end_utc < d->window_start || end_utc > d->window_end) return true;
    if ((d->meeting_key > 0 && meeting != d->meeting_key) ||
        (d->reminder_meeting > 0 && meeting != d->reminder_meeting) || d->keys[kind]) {
        d->valid = false; return false;
    }
    for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) {
        if (d->keys[i] == key) { d->valid = false; return false; }
    }
    d->keys[kind] = key;
    d->reminder_meeting = meeting;
    parse_reminder_session(item, d);
    update_session_identity(&d->cache->sessions[kind], key, cancelled, end_utc);
    d->matched = true;
    return true;
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

    // Do not publish a prefix of an interrupted or malformed response.
    race_cache_t parsed = *cache;
    session_discovery_t discovery = {
        .cache = &parsed,
        .valid = true,
        .reminder_valid = true,
        .meeting_key = race.meeting_key,
        .window_start = race.switch_at_utc - RESULTS_WINDOW_SECONDS,
        .window_end = race.switch_at_utc,
    };
    if (pdkpass_http_array(url, parse_discovered_session, &discovery) != ESP_OK)
        return false;
    if (!discovery.valid) return false;
    if (discovery.matched) parsed.discovered = 1;
    if (discovery.reminder_matched && discovery.reminder_valid)
        pdkpass_reminder_update_round(pdkpass_season_year(), race.round,
                                      discovery.reminder_meeting, discovery.reminders);
    *cache = parsed;
    cache->last_discovery_utc = now_utc;
    return discovery.matched;
}

static bool parse_podium_item(const cJSON *item, void *user)
{
    result_driver_t *top = user;
    const cJSON *position = cJSON_GetObjectItemCaseSensitive(item, "position");
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "driver_number");
    int32_t place, driver;
    if (!json_positive_key(position, &place) || !json_positive_key(number, &driver) || driver > 99)
        return false;
    if (place > PDKPASS_PODIUM_SIZE) return true;
    if (top[place - 1].position) return false;
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++)
        if (top[i].driver_number == driver) return false;
    top[place - 1].position = (unsigned)place;
    top[place - 1].driver_number = driver;
    return true;
}

static bool podium_complete(const result_driver_t top[PDKPASS_PODIUM_SIZE])
{
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        if (top[i].position != i + 1 || top[i].driver_number <= 0) return false;
    }
    return true;
}

static void copy_json_text(char *destination, size_t capacity,
                           const cJSON *object, const char *name,
                           const char *fallback)
{
    if (!capacity) return;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    const char *text = cJSON_IsString(item) ? item->valuestring : fallback;
    size_t length = strlen(text);
    if (length >= capacity) length = capacity - 1;
    memcpy(destination, text, length);
    destination[length] = '\0';
}

typedef struct {
    const result_driver_t *top;
    char (*podium_codes)[4];
} podium_details_t;

static bool parse_podium_driver(const cJSON *item, void *user)
{
    podium_details_t *details = user;
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "driver_number");
    int32_t driver;
    if (!json_positive_key(number, &driver) || driver > 99) return false;
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        if (driver != details->top[i].driver_number) continue;
        if (details->podium_codes[i][0]) return false;
        char fallback[8];
        snprintf(fallback, sizeof(fallback), "#%d", details->top[i].driver_number);
        copy_json_text(details->podium_codes[i], sizeof(details->podium_codes[i]),
                       item, "name_acronym", fallback);
    }
    return true;
}

static bool podium_drivers_complete(const char podium_codes[PDKPASS_PODIUM_SIZE][4])
{
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
    result_driver_t top[PDKPASS_PODIUM_SIZE] = {0};
    if (pdkpass_http_array(url, parse_podium_item, top) != ESP_OK ||
        !podium_complete(top)) return false;

    char podium_codes[PDKPASS_PODIUM_SIZE][4] = {0};
    podium_details_t details = {.top = top, .podium_codes = podium_codes};
    // The full grid arrives in an 8-10 KB TLS record on OpenF1. Streaming
    // JSON cannot shrink that record: TLS must first allocate/decrypt it.
    // Fetch only the three required drivers, keeping each response small.
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        snprintf(url, sizeof(url),
                 "https://api.openf1.org/v1/drivers?session_key=%ld&driver_number=%d",
                 (long)session->session_key, top[i].driver_number);
        if (pdkpass_http_array(url, parse_podium_driver, &details) != ESP_OK ||
            podium_codes[i][0] == '\0') return false;
    }
    bool parsed = podium_drivers_complete((const char (*)[4])podium_codes);
    if (parsed) {
        memcpy(session->podium_codes, podium_codes, sizeof(podium_codes));
        session->ready = 1;
    }
    return parsed;
}
