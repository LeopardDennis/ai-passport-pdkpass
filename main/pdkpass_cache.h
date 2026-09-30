#pragma once
#include "esp_err.h"
#include <stddef.h>

#define PDKPASS_CACHE_PARTITION "pdk_cache"

// Initialize the private partition before services without clearing data.
// Reboots and firmware upgrades retain valid caches and sync dates. Failed
// initialization disables cache access; it never erases a partition.
esp_err_t pdkpass_cache_init(void);
// Read/write only the private partition; never import old default-NVS data.
esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size);
esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size);
void pdkpass_cache_forget_namespace(const char *ns);
