#include "pdkpass_cache.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "pdk_cache";
static bool s_ready;

// Explicit invalidation within the current image touches only this namespace.
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

esp_err_t pdkpass_cache_init(void)
{
    s_ready = false;
    esp_err_t err = nvs_flash_init_partition(PDKPASS_CACHE_PARTITION);
    const esp_app_desc_t *app = esp_app_get_description();
    if (err == ESP_OK) {
        uint8_t image[sizeof(app->app_elf_sha256)];
        size_t size = sizeof(image);
        nvs_handle_t handle;
        err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, "pdk_meta", NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, "image", image, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && size == sizeof(image) &&
            memcmp(image, app->app_elf_sha256, sizeof(image)) == 0) {
            // Same image: initialize settings without erasing Wi-Fi or caches.
            err = nvs_flash_init_partition("nvs");
            s_ready = err == ESP_OK;
            return err;
        }
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND &&
            err != ESP_ERR_NVS_INVALID_LENGTH && err != ESP_ERR_NVS_TYPE_MISMATCH)
            return err;
    } else if (err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return err;
    }

    // Reset ALL application storage, not a list of today's known keys. These
    // two named NVS partitions contain settings and data, never cardid/Recovery.
    // erase_partition deinitializes an open partition before erasing it.
    err = nvs_flash_erase_partition("nvs");
    if (err != ESP_OK) return err;
    err = nvs_flash_init_partition("nvs");
    if (err != ESP_OK) return err;
    err = nvs_flash_erase_partition(PDKPASS_CACHE_PARTITION);
    if (err != ESP_OK) return err;
    err = nvs_flash_init_partition(PDKPASS_CACHE_PARTITION);
    if (err != ESP_OK) return err;

    // Commit ownership only after both partitions are fresh. Failed/interrupted
    // cleanup leaves access disabled and startup must stop before any service
    // can load old settings or connect with old credentials. Retry next boot.
    nvs_handle_t handle;
    err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, "pdk_meta", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "image", app->app_elf_sha256, sizeof(app->app_elf_sha256));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    s_ready = true;
    ESP_LOGW(TAG, "Firmware changed; reset all application data and settings");
    return ESP_OK;
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
