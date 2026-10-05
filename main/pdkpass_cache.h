#pragma once
#include "esp_err.h"
#include <stddef.h>

#define PDKPASS_CACHE_PARTITION "pdk_cache"

// Initialize application storage before ANY service/network/settings load.
// Preserve compatible data/settings across firmware changes and power cycles.
// Ignore legacy image-owner markers; services validate their snapshot formats.
// Recover unreadable NVS formats only in the affected application partition.
// cardid/Recovery are never touched. Failures stop startup and retry next boot.
esp_err_t pdkpass_cache_init(void);
// Read/write only the private partition; never import old default-NVS data.
esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size);
esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size);
void pdkpass_cache_forget_namespace(const char *ns);
