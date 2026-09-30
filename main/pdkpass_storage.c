#include "pdkpass_storage.h"
#include "nvs.h"
esp_err_t pdkpass_storage_erase(const char *ns, const char *const *keys, size_t count)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < count && err == ESP_OK; i++) {
        err = nvs_erase_key(handle, keys[i]);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
