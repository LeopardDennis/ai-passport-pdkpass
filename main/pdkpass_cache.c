#include "pdkpass_cache.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "pdk_cache";

esp_err_t pdkpass_cache_init(void)
{
    return nvs_flash_init_partition(PDKPASS_CACHE_PARTITION);
}

esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size)
{
    nvs_handle_t handle;
    size_t capacity = *size;
    esp_err_t err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READONLY, &handle);
    bool opened = err == ESP_OK;
    if (opened) {
        err = nvs_get_blob(handle, key, data, size);
        nvs_close(handle);
    }
    if (opened && err != ESP_ERR_NVS_NOT_FOUND) return err;
    // An unavailable new partition still permits reading the old cache.
    // A partially migrated namespace may already contain another key. Fall
    // back per key, not per namespace, and restore the caller's buffer capacity.
    *size = capacity;
    err = nvs_open(ns, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, key, data, size);
        nvs_close(handle);
    }
    return err;
}

esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, key, data, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) return err;

    // Clean up only a key for which a new copy has been committed. Failure to
    // reclaim the old key must not turn an already persisted update into FAIL.
    esp_err_t cleanup = nvs_open(ns, NVS_READONLY, &handle);
    if (cleanup == ESP_OK) {
        nvs_close(handle);
        cleanup = nvs_open(ns, NVS_READWRITE, &handle);
        if (cleanup == ESP_OK) {
            cleanup = nvs_erase_key(handle, key);
            if (cleanup == ESP_OK) cleanup = nvs_commit(handle);
            nvs_close(handle);
        }
    }
    if (cleanup != ESP_OK && cleanup != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW(TAG, "Legacy cache cleanup failed: %s", esp_err_to_name(cleanup));
    return ESP_OK;
}

void pdkpass_cache_forget_namespace(const char *ns)
{
    nvs_handle_t handle;
    if (nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_erase_all(handle) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}
