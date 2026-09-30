// Private implementation: included by the owning service, with its existing lock and worker.
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

static bool restore_legacy_cache(const legacy_season_cache_t *stored)
{
    if (!stored || stored->season.year < 2026U ||
        stored->season.year > 2100U || stored->season.race_count == 0U ||
        stored->season.race_count > PDKPASS_MAX_RACES ||
        stored->season.driver_count > PDKPASS_MAX_DRIVERS) return false;
    const legacy_season_snapshot_t *old = &stored->season;
    for (size_t i = 0; i < old->race_count; i++) {
        if (old->races[i].round == 0U ||
            (i > 0U && old->races[i - 1U].switch_at_utc >=
                           old->races[i].switch_at_utc)) return false;
    }
    memset(&s_season, 0, sizeof(s_season));
    s_season.year = old->year;
    s_season.race_count = old->race_count;
    s_season.driver_count = old->driver_count;
    memcpy(s_season.standings_as_of, old->standings_as_of,
           sizeof(s_season.standings_as_of));
    memcpy(s_season.races, old->races, sizeof(s_season.races));
    for (size_t i = 0; i < old->driver_count; i++) {
        const legacy_driver_t *source = &old->drivers[i];
        pdkpass_driver_t *target = &s_season.drivers[i];
        target->accent = source->accent;
        target->points_tenths = source->points_tenths;
        target->position = source->position;
        target->driver_number = source->driver_number;
        memcpy(target->code, source->code, sizeof(target->code));
        memcpy(target->name, source->name, sizeof(target->name));
        memcpy(target->team, source->team, sizeof(target->team));
        fill_known_first_name(target);
    }
    return snapshot_valid(&s_season);
}

// Older calendar-only syncs could persist the bundled points as if downloaded.
// Match the entire known snapshot, not just a date or one driver's score, so
// other previously synchronized standings remain available offline.
static void discard_bundled_standings(void)
{
    if (s_season.year != 2026U ||
        s_season.driver_count != pdkpass_legacy_driver_count ||
        strncmp(s_season.standings_as_of, "31 AUG", sizeof(s_season.standings_as_of)) != 0)
        return;
    for (size_t i = 0; i < pdkpass_legacy_driver_count; i++) {
        const pdkpass_driver_t *cached = &s_season.drivers[i];
        const pdkpass_driver_t *legacy = &pdkpass_legacy_drivers[i];
        if (cached->position != legacy->position ||
            cached->driver_number != legacy->driver_number ||
            cached->points_tenths != legacy->points_tenths ||
            memcmp(cached->code, legacy->code, sizeof(cached->code)) != 0)
            return;
    }
    s_season.driver_count = 0;
    memset(s_season.drivers, 0, sizeof(s_season.drivers));
    copy_text(s_season.standings_as_of, sizeof(s_season.standings_as_of), "PENDING");
}

static void load_cache(void)
{
    initialize_fallback();
    s_has_cached_data = false;
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
    bool loaded = false;
    if (err == ESP_OK &&
        (size == sizeof(*stored) || size == sizeof(legacy_season_cache_t)) &&
        stored->magic == SEASON_CACHE_MAGIC) {
        if (size == sizeof(*stored) &&
            stored->version == SEASON_CACHE_VERSION &&
            snapshot_valid(&stored->season)) {
            s_season = stored->season;
            loaded = true;
        } else if (size == sizeof(legacy_season_cache_t) &&
                   stored->version == SEASON_CACHE_LEGACY_VERSION) {
            loaded = restore_legacy_cache((const legacy_season_cache_t *)stored);
        }
    }
    if (loaded) {
        discard_bundled_standings();
        s_has_cached_data = true;
        s_season.race_count = (uint8_t)pdkpass_restore_legacy_calendar(
            s_season.year, s_season.races, s_season.race_count,
            PDKPASS_MAX_RACES);
        for (size_t i = 0; i < s_season.race_count; i++)
            apply_track_details(&s_season.races[i], s_season.races[i].circuit);
    }
    free(stored);
}

static esp_err_t save_cache(const pdkpass_season_snapshot_t *season)
{
    pdkpass_http_release();
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
