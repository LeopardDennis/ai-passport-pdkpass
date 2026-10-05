#include "pdkpass_cache.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "pdk_cache";
static bool s_ready;

// Invalidating an incompatible snapshot touches only its own namespace.
static esp_err_t clear_data_namespace(const char *ns)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    nvs_close(handle);
    err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// Recover an unreadable NVS format only in the affected application partition.
// A new firmware image is not a reason to discard compatible data/settings.
static esp_err_t init_partition(const char *partition)
{
    esp_err_t err = nvs_flash_init_partition(partition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Recovering unreadable NVS partition: %s", partition);
        err = nvs_flash_erase_partition(partition);
        if (err == ESP_OK) err = nvs_flash_init_partition(partition);
    }
    return err;
}

esp_err_t pdkpass_cache_init(void)
{
    s_ready = false;
    esp_err_t err = init_partition("nvs");
    if (err == ESP_OK) err = init_partition(PDKPASS_CACHE_PARTITION);
    s_ready = err == ESP_OK;
    return err;
}

esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, key, data, size);
        nvs_close(handle);
    }
    return err;
}

esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, ns, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, key, data, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

void pdkpass_cache_forget_namespace(const char *ns)
{
    if (!s_ready) return;
    esp_err_t err = clear_data_namespace(ns);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Cache cleanup failed: %s", esp_err_to_name(err));
}
