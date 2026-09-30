// Private implementation: included by the owning service, with its existing lock and worker.
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
        // The old offline calendar stored R13-R23 at indexes 0-10. Shift
        // only its unkeyed cache; keyed API results still match by identity.
        if (stored->year == 2026U && stored->race_count == 11U &&
            pdkpass_season_race_count() == 23U && index >= 12U) {
            bool legacy = true;
            for (size_t i = 0; i < stored->race_count; i++) {
                if (stored->races[i].meeting_key != 0) legacy = false;
            }
            if (legacy) return (int)(index - 12U);
        }
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
    bool cached_data = false;
    {
        size_t size = sizeof(*stored);
        esp_err_t err = pdkpass_cache_read_blob(NVS_NAMESPACE, NVS_KEY, stored, &size);
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
        size_t race_count = pdkpass_season_race_count();
        valid = err == ESP_OK && size == sizeof(*stored) &&
                persisted_matches(stored, pdkpass_season_year(), race_count);
        if (valid) {
            for (size_t i = 0; i < race_count; i++) {
                int match = stored_index(stored, i, &loaded[i]);
                if (match < 0) continue;
                const persisted_race_t *source = &stored->races[match];
                cached_data |= source->discovered || source->present_mask ||
                               source->cancelled_mask || source->ready_mask;
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
        s_cache_retry_at_us = 0;
        s_has_cached_data = cached_data;
        s_requested_race = SIZE_MAX;
        s_priority_race = SIZE_MAX;
        s_priority_until_utc = 0;
        xSemaphoreGive(s_lock);
    }
    if (!valid) pdkpass_cache_forget_namespace(NVS_NAMESPACE);
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
    bool cached_data = false;
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
        cached_data |= stored->races[i].discovered ||
                       stored->races[i].present_mask ||
                       stored->races[i].cancelled_mask ||
                       stored->races[i].ready_mask;
    }
    xSemaphoreGive(s_lock);

    esp_err_t err = pdkpass_cache_write_blob(NVS_NAMESPACE, NVS_KEY, stored, sizeof(*stored));
    if (err == ESP_OK && xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        s_has_cached_data = cached_data;
        xSemaphoreGive(s_lock);
    }
    free(stored);
    return err;
}

// Caller owns the shared transaction, serializing this NVS snapshot with
// season replacement and results publication. No network request is needed.
static esp_err_t save_cache_with_retry(void)
{
    pdkpass_http_release();
    esp_err_t err = save_cache();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cache_dirty = err != ESP_OK;
    s_cache_retry_at_us = err == ESP_OK ? 0 :
        esp_timer_get_time() + (int64_t)RESULTS_SAVE_RETRY_MS * 1000;
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) ESP_LOGW(TAG, "Results cache save failed: %s", esp_err_to_name(err));
    return err;
}

static uint32_t cache_retry_wait_ms(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return RESULTS_SAVE_BUSY_RETRY_MS;
    int64_t deadline = s_cache_retry_at_us;
    xSemaphoreGive(s_lock);
    if (!deadline) return UINT32_MAX;
    int64_t remaining = deadline - esp_timer_get_time();
    if (remaining <= 0) return 0;
    int64_t ms = (remaining + 999) / 1000;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static TickType_t cache_retry_wait_ticks(TickType_t network_wait)
{
    uint32_t ms = cache_retry_wait_ms();
    if (ms == UINT32_MAX) return network_wait;
    TickType_t retry = pdMS_TO_TICKS(ms);
    if (!retry) retry = 1;
    return retry < network_wait ? retry : network_wait;
}

static void retry_cache_if_due(void)
{
    if (cache_retry_wait_ms() != 0) return;
    if (!pdkpass_http_try_begin()) {
        // A season transaction owns the cache generation. Retry shortly rather
        // than block behind HTTP or spin on an already expired local deadline.
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_cache_retry_at_us)
            s_cache_retry_at_us = esp_timer_get_time() +
                (int64_t)RESULTS_SAVE_BUSY_RETRY_MS * 1000;
        xSemaphoreGive(s_lock);
        return;
    }
    // Recheck after acquiring ownership: the season could have replaced and
    // cleared this dirty cache while we waited for the transaction.
    if (s_cache_dirty) save_cache_with_retry();
    pdkpass_http_end();
}
