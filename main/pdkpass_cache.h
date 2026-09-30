#pragma once
#include "esp_err.h"
#include <stddef.h>

#define PDKPASS_CACHE_PARTITION "pdk_cache"

// Separate renewable data from the small system/credential NVS partition.
// Initialize before starting services. No partition is erased on init failure.
esp_err_t pdkpass_cache_init(void);
// Read the private cache first, falling back to the existing default-NVS blob
// until a successful write migrates that key. Storage formats stay unchanged.
esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size);
// Commit to the private partition before removing that one legacy cache key.
// Credentials, device identity and other namespaces are never erased.
esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size);
void pdkpass_cache_forget_namespace(const char *ns);
