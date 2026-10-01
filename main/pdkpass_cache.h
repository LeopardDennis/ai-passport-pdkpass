#pragma once
#include "esp_err.h"
#include <stddef.h>

#define PDKPASS_CACHE_PARTITION "pdk_cache"

// Initialize application storage before ANY service/network/settings load.
// A changed/missing/invalid app_elf_sha256 owner erases both nvs and pdk_cache:
// Wi-Fi profiles, saved time, sync dates, reminders, toggles and downloaded data.
// Identical-image power cycles retain valid data. cardid and Recovery are never
// touched. Commit ownership last; failed cleanup must stop application startup
// and retry next boot, so old settings cannot be used after a partial reset.
esp_err_t pdkpass_cache_init(void);
// Read/write only the private partition; never import old default-NVS data.
esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size);
esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size);
void pdkpass_cache_forget_namespace(const char *ns);
