// Private implementation: standings transport, validation and scoped persistence.
static bool jolpica_decimal(const char *text, unsigned maximum,
                             bool tenths, unsigned *value)
{
    if (!text || *text < '0' || *text > '9') return false;
    unsigned result = 0;
    while (*text >= '0' && *text <= '9') {
        result = result * 10U + (unsigned)(*text++ - '0');
        if (result > maximum) return false;
    }
    if (tenths) {
        result *= 10U;
        if (*text == '.' && text[1] >= '0' && text[1] <= '9') {
            result += (unsigned)(text[1] - '0');
            text += 2;
        }
    }
    if (*text || result > maximum) return false;
    *value = result;
    return true;
}

static bool jolpica_uint(const cJSON *object, const char *key,
                          unsigned maximum, unsigned *value)
{
    return jolpica_decimal(json_string(object, key), maximum, false, value);
}

static cJSON *jolpica_get(const char *url)
{
    char *body = NULL;
    // Four drivers normally occupy about 2 KB. Bound both HTTP and JSON memory.
    if (pdkpass_http_get(url, 4096, &body) != ESP_OK) return NULL;
    cJSON *root = cJSON_ParseWithOpts(body, NULL, true);
    if (!root) pdkpass_http_report_data_failure(
        "jolpica-json", ESP_ERR_INVALID_RESPONSE, strlen(body));
    free(body);
    return root;
}

static void jolpica_constructor(const cJSON *team, char *name, size_t size,
                                  uint32_t *accent)
{
    static const struct { const char *id, *name; uint32_t colour; } teams[] = {
        {"mercedes", "MERCEDES", 0x00A19C}, {"ferrari", "FERRARI", 0xE32636},
        {"mclaren", "MCLAREN", 0xFF8700}, {"red_bull", "RED BULL", 0x3671C6},
        {"rb", "RACING BULLS", 0x6692FF}, {"alpine", "ALPINE", 0x2293D1},
        {"haas", "HAAS", 0xB6BABD}, {"audi", "AUDI", 0xF50537},
        {"williams", "WILLIAMS", 0x64C4FF},
        {"aston_martin", "ASTON MARTIN", 0x229971},
        {"cadillac", "CADILLAC", 0x1B2D57},
    };
    *accent = 0x3671C6;
    const char *id = json_string(team, "constructorId");
    copy_upper(name, size, json_string(team, "name"));
    for (size_t i = 0; i < sizeof(teams) / sizeof(teams[0]); i++) {
        if (id && strcmp(id, teams[i].id) == 0) {
            copy_text(name, size, teams[i].name);
            *accent = teams[i].colour;
            return;
        }
    }
}

static void jolpica_team(const cJSON *constructors, pdkpass_driver_t *driver)
{
    driver->accent = 0x3671C6;
    if (cJSON_GetArraySize(constructors) != 1) {
        copy_text(driver->team, sizeof(driver->team), "MULTIPLE TEAMS");
        return;
    }
    jolpica_constructor(cJSON_GetArrayItem(constructors, 0), driver->team,
                         sizeof(driver->team), &driver->accent);
}

static bool jolpica_driver(const cJSON *item, pdkpass_driver_t *drivers,
                            size_t count)
{
    const cJSON *driver = cJSON_GetObjectItemCaseSensitive(item, "Driver");
    const cJSON *teams = cJSON_GetObjectItemCaseSensitive(item, "Constructors");
    const char *code = json_string(driver, "code");
    const char *name = json_string(driver, "familyName");
    const char *first = json_string(driver, "givenName");
    unsigned position, number, points;
    if (!jolpica_uint(item, "position", PDKPASS_MAX_DRIVERS, &position) ||
        position != count + 1U ||
        !jolpica_uint(driver, "permanentNumber", 255, &number) || !number ||
        !jolpica_decimal(json_string(item, "points"), 65535, true, &points) ||
        !code || strlen(code) != 3U || !name || !*name || !first || !*first ||
        !cJSON_IsArray(teams) || cJSON_GetArraySize(teams) < 1) return false;
    for (size_t i = 0; i < 3U; i++)
        if (code[i] < 'A' || code[i] > 'Z') return false;
    for (size_t i = 0; i < count; i++) {
        if (drivers[i].driver_number == number ||
            strcmp(drivers[i].code, code) == 0) return false;
    }
    for (int i = 0; i < cJSON_GetArraySize(teams); i++) {
        const cJSON *team = cJSON_GetArrayItem(teams, i);
        const char *id = json_string(team, "constructorId");
        const char *team_name = json_string(team, "name");
        if (!id || !*id || !team_name || !*team_name) return false;
    }
    pdkpass_driver_t *out = &drivers[count];
    out->position = (uint8_t)position;
    out->driver_number = (uint8_t)number;
    out->points_tenths = (uint16_t)points;
    copy_text(out->code, sizeof(out->code), code);
    copy_upper(out->name, sizeof(out->name), name);
    copy_upper(out->first_name, sizeof(out->first_name), first);
    fill_known_first_name(out);
    jolpica_team(teams, out);
    return true;
}

static unsigned standings_date_order(const char *date)
{
    static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (!date || strlen(date) != 6U || date[2] != ' ') return 0;
    unsigned day = 0;
    char text[3] = {date[0], date[1], 0};
    if (!jolpica_decimal(text, 31, false, &day) || !day) return 0;
    for (unsigned i = 0; i < 12U; i++)
        if (strcmp(date + 3, months[i]) == 0) return (i + 1U) * 32U + day;
    return 0;
}

static bool jolpica_standings_date(unsigned year, unsigned round, int64_t now_utc,
                                     const char *previous_date, char as_of[12])
{
    char url[160];
    // Resolve the source date from Jolpica itself: OpenF1 availability and
    // calendar round numbering must not gate or misdate championship points.
    vTaskDelay(pdMS_TO_TICKS(300));
    snprintf(url, sizeof(url), "https://api.jolpi.ca/ergast/f1/%u/%u/",
             year, round);
    cJSON *root = jolpica_get(url);
    const cJSON *mr = cJSON_GetObjectItemCaseSensitive(root, "MRData");
    const cJSON *table = cJSON_GetObjectItemCaseSensitive(mr, "RaceTable");
    const cJSON *races = cJSON_GetObjectItemCaseSensitive(table, "Races");
    const cJSON *race = cJSON_GetArrayItem(races, 0);
    unsigned response_year, race_round;
    const char *date = json_string(race, "date");
    const char *time = json_string(race, "time");
    char stamp[40];
    int64_t race_utc = 0;
    bool valid = cJSON_IsArray(races) && cJSON_GetArraySize(races) == 1 &&
        jolpica_uint(race, "season", 2100, &response_year) && response_year == year &&
        jolpica_uint(race, "round", PDKPASS_MAX_RACES, &race_round) && race_round == round &&
        date && strlen(date) == 10U && (!time || strlen(time) == 9U);
    if (valid) {
        snprintf(stamp, sizeof(stamp), "%sT%s", date, time ? time : "00:00:00Z");
        valid = pdkpass_parse_iso8601_utc(stamp, &race_utc) &&
                strtoul(date, NULL, 10) == year &&
                race_utc <= now_utc + 3LL * 86400LL;
        // Published standings can already include a Sprint before Sunday's
        // race. The round's scheduled date is a label, not a completion gate.
    }
    cJSON_Delete(root);
    if (!valid) return false;
    pdkpass_format_beijing_date(race_utc, as_of, 12);
    if (standings_date_order(as_of) < standings_date_order(previous_date))
        return false;
    return true;
}

static bool fetch_standings(int64_t now_utc, pdkpass_season_snapshot_t *candidate)
{
    char url[160];
    pdkpass_driver_t parsed[PDKPASS_MAX_DRIVERS] = {0};
    unsigned total = 0, round = 0, count = 0;
    do {
        if (count) {
            // Pin subsequent pages to the first response's round, preventing
            // a newly published round from shifting the pagination midway.
            snprintf(url, sizeof(url),
                "https://api.jolpi.ca/ergast/f1/%u/%u/driverstandings/?limit=4&offset=%u",
                candidate->year, round, count);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            snprintf(url, sizeof(url),
                "https://api.jolpi.ca/ergast/f1/%u/driverstandings/?limit=4&offset=0",
                candidate->year);
        }
        cJSON *root = jolpica_get(url);
        const cJSON *mr = cJSON_GetObjectItemCaseSensitive(root, "MRData");
        const cJSON *table = cJSON_GetObjectItemCaseSensitive(mr, "StandingsTable");
        const cJSON *lists = cJSON_GetObjectItemCaseSensitive(table, "StandingsLists");
        const cJSON *list = cJSON_GetArrayItem(lists, 0);
        const cJSON *rows = cJSON_GetObjectItemCaseSensitive(list, "DriverStandings");
        unsigned page_total, offset, limit, year, page_round;
        bool valid = cJSON_IsArray(lists) && cJSON_GetArraySize(lists) == 1 &&
            jolpica_uint(mr, "total", PDKPASS_MAX_DRIVERS, &page_total) && page_total &&
            jolpica_uint(mr, "offset", PDKPASS_MAX_DRIVERS, &offset) && offset == count &&
            jolpica_uint(mr, "limit", 4, &limit) && limit == 4 &&
            jolpica_uint(list, "season", 2100, &year) && year == candidate->year &&
            jolpica_uint(list, "round", PDKPASS_MAX_RACES, &page_round) && page_round &&
            cJSON_IsArray(rows);
        if (valid && count == 0) { total = page_total; round = page_round; }
        unsigned expected = total - count;
        if (expected > 4U) expected = 4U;
        valid = valid && page_total == total && page_round == round &&
                (unsigned)cJSON_GetArraySize(rows) == expected;
        for (unsigned i = 0; valid && i < expected; i++)
            valid = jolpica_driver(cJSON_GetArrayItem(rows, (int)i), parsed, count + i);
        cJSON_Delete(root);
        if (!valid) return false;
        count += expected;
    } while (count < total);

    char as_of[12];
    if (!jolpica_standings_date(candidate->year, round, now_utc,
                                candidate->standings_as_of, as_of)) return false;
    candidate->driver_count = (uint8_t)total;
    memset(candidate->drivers, 0, sizeof(candidate->drivers));
    memcpy(candidate->drivers, parsed, total * sizeof(parsed[0]));
    copy_text(candidate->standings_as_of, sizeof(candidate->standings_as_of), as_of);
    return true;
}

static bool jolpica_team_item(const cJSON *item, pdkpass_team_t *teams, size_t count)
{
    const cJSON *constructor = cJSON_GetObjectItemCaseSensitive(item, "Constructor");
    const char *id = json_string(constructor, "constructorId");
    const char *name = json_string(constructor, "name");
    unsigned position, points;
    if (!jolpica_uint(item, "position", PDKPASS_MAX_TEAMS, &position) ||
        position != count + 1U || !id || !*id || strlen(id) >= sizeof(teams[0].id) ||
        !name || !*name ||
        !jolpica_decimal(json_string(item, "points"), 65535, true, &points)) return false;
    for (size_t i = 0; i < count; i++)
        if (strcmp(teams[i].id, id) == 0) return false;
    pdkpass_team_t *out = &teams[count];
    out->position = (uint8_t)position;
    out->points_tenths = (uint16_t)points;
    copy_text(out->id, sizeof(out->id), id);
    jolpica_constructor(constructor, out->name, sizeof(out->name), &out->accent);
    return true;
}

static bool fetch_team_standings(int64_t now_utc, pdkpass_team_snapshot_t *candidate)
{
    char url[160];
    pdkpass_team_t parsed[PDKPASS_MAX_TEAMS] = {0};
    unsigned total = 0, round = 0, count = 0;
    do {
        if (count) {
            // Pin subsequent pages to the first response's round, preventing
            // a newly published round from shifting the pagination midway.
            snprintf(url, sizeof(url),
                "https://api.jolpi.ca/ergast/f1/%u/%u/constructorstandings/?limit=4&offset=%u",
                candidate->year, round, count);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            snprintf(url, sizeof(url),
                "https://api.jolpi.ca/ergast/f1/%u/constructorstandings/?limit=4&offset=0",
                candidate->year);
        }
        cJSON *root = jolpica_get(url);
        const cJSON *mr = cJSON_GetObjectItemCaseSensitive(root, "MRData");
        const cJSON *table = cJSON_GetObjectItemCaseSensitive(mr, "StandingsTable");
        const cJSON *lists = cJSON_GetObjectItemCaseSensitive(table, "StandingsLists");
        const cJSON *list = cJSON_GetArrayItem(lists, 0);
        const cJSON *rows = cJSON_GetObjectItemCaseSensitive(list, "ConstructorStandings");
        unsigned page_total, offset, limit, year, page_round;
        bool valid = cJSON_IsArray(lists) && cJSON_GetArraySize(lists) == 1 &&
            jolpica_uint(mr, "total", PDKPASS_MAX_TEAMS, &page_total) && page_total &&
            jolpica_uint(mr, "offset", PDKPASS_MAX_TEAMS, &offset) && offset == count &&
            jolpica_uint(mr, "limit", 4, &limit) && limit == 4 &&
            jolpica_uint(list, "season", 2100, &year) && year == candidate->year &&
            jolpica_uint(list, "round", PDKPASS_MAX_RACES, &page_round) && page_round &&
            cJSON_IsArray(rows);
        if (valid && count == 0) { total = page_total; round = page_round; }
        unsigned expected = total - count;
        if (expected > 4U) expected = 4U;
        valid = valid && page_total == total && page_round == round &&
                (unsigned)cJSON_GetArraySize(rows) == expected;
        for (unsigned i = 0; valid && i < expected; i++)
            valid = jolpica_team_item(cJSON_GetArrayItem(rows, (int)i), parsed, count + i);
        cJSON_Delete(root);
        if (!valid) return false;
        count += expected;
    } while (count < total);

    char as_of[12];
    if (!jolpica_standings_date(candidate->year, round, now_utc,
                                candidate->as_of, as_of)) return false;
    candidate->count = (uint8_t)total;
    memset(candidate->teams, 0, sizeof(candidate->teams));
    memcpy(candidate->teams, parsed, total * sizeof(parsed[0]));
    copy_text(candidate->as_of, sizeof(candidate->as_of), as_of);
    return true;
}

static bool team_snapshot_valid(const pdkpass_team_snapshot_t *snapshot)
{
    if (snapshot->year < 2026 || snapshot->year > 2100 ||
        snapshot->count == 0 || snapshot->count > PDKPASS_MAX_TEAMS ||
        !memchr(snapshot->as_of, '\0', sizeof(snapshot->as_of)) ||
        standings_date_order(snapshot->as_of) == 0) return false;
    for (size_t i = 0; i < snapshot->count; i++) {
        const pdkpass_team_t *team = &snapshot->teams[i];
        if (team->position != i + 1U || !team->id[0] || !team->name[0] ||
            !memchr(team->id, '\0', sizeof(team->id)) ||
            !memchr(team->name, '\0', sizeof(team->name))) return false;
        for (size_t j = 0; j < i; j++)
            if (strcmp(team->id, snapshot->teams[j].id) == 0) return false;
    }
    return true;
}

static void load_team_cache(void)
{
    memset(&s_teams, 0, sizeof(s_teams));
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    team_cache_t stored = {0};
    size_t size = sizeof(stored);
    esp_err_t err = nvs_get_blob(handle, "teams", &stored, &size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(stored) &&
        stored.magic == TEAM_CACHE_MAGIC && stored.version == 1 &&
        team_snapshot_valid(&stored.snapshot)) s_teams = stored.snapshot;
}

static esp_err_t save_team_cache(const pdkpass_team_snapshot_t *snapshot)
{
    pdkpass_http_release();
    team_cache_t stored = {.magic = TEAM_CACHE_MAGIC, .version = 1,
                           .snapshot = *snapshot};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "teams", &stored, sizeof(stored));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

bool pdkpass_season_team_snapshot(pdkpass_team_snapshot_t *snapshot)
{
    if (!snapshot || !s_lock ||
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    if (s_teams.year == s_season.year) *snapshot = s_teams;
    else {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->year = s_season.year;
        copy_text(snapshot->as_of, sizeof(snapshot->as_of), "PENDING");
    }
    xSemaphoreGive(s_lock);
    return true;
}

static bool synchronize_teams(unsigned year, int64_t now_utc)
{
    pdkpass_team_snapshot_t candidate;
    if (!pdkpass_season_team_snapshot(&candidate) || candidate.year != year)
        return false;
    if (!fetch_team_standings(now_utc, &candidate)) {
        ESP_LOGW(TAG, "Jolpica team standings unchanged; retry scheduled");
        return false;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool changed = memcmp(&s_teams, &candidate, sizeof(candidate)) != 0;
    xSemaphoreGive(s_lock);
    if (!changed) {
        pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_TEAMS, (int64_t)time(NULL));
        return true;
    }
    if (save_team_cache(&candidate) != ESP_OK) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    s_teams = candidate;
    xSemaphoreGive(s_lock);
    if (s_callback) s_callback();
    pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_TEAMS, (int64_t)time(NULL));
    return true;
}
