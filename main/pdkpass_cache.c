#include "pdkpass_cache.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "pdk_cache";
static bool s_ready;

// Check existence without creating empty namespaces. Never erase a partition:
// default NVS also holds Wi-Fi, clock and system settings.
static esp_err_t clear_data_namespace(const char *partition, const char *ns)
{
    nvs_handle_t handle;
    esp_err_t err = partition ? nvs_open_from_partition(partition, ns, NVS_READONLY, &handle)
                              : nvs_open(ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    nvs_close(handle);
    err = partition ? nvs_open_from_partition(partition, ns, NVS_READWRITE, &handle)
                    : nvs_open(ns, NVS_READWRITE, &handle);
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
    if (err != ESP_OK) return err;
    const esp_app_desc_t *app = esp_app_get_description();
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
        s_ready = true;
        return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND &&
        err != ESP_ERR_NVS_INVALID_LENGTH) return err;

    // A different image starts with fresh downloadable data. Keep reminder
    // preferences, credentials, clock, card identity and Recovery untouched.
    const char *const namespaces[] = {"pdk_season", "pdk_results"};
    for (size_t i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); i++) {
        err = clear_data_namespace(PDKPASS_CACHE_PARTITION, namespaces[i]);
        if (err != ESP_OK) return err;
        err = clear_data_namespace(NULL, namespaces[i]);
        if (err != ESP_OK) return err;
    }
    err = clear_data_namespace(NULL, "pdk_sync");
    if (err != ESP_OK) return err;

    // Commit the image marker last. Failed/interrupted cleanup retries at the
    // next boot; no reads or writes can expose an old cache in the meantime.
    err = nvs_open_from_partition(PDKPASS_CACHE_PARTITION, "pdk_meta", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "image", app->app_elf_sha256, sizeof(image));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    s_ready = true;
    ESP_LOGW(TAG, "Firmware changed; cleared calendar, standings, results and sync dates");
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
    esp_err_t err = clear_data_namespace(PDKPASS_CACHE_PARTITION, ns);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Cache cleanup failed: %s", esp_err_to_name(err));
}
