#pragma once
#include "esp_err.h"
#include <stddef.h>

#define PDKPASS_CACHE_PARTITION "pdk_cache"

// Initialize before services. A different firmware image clears downloaded
// calendar/standings/results and their sync dates in private and legacy NVS.
// Reboots of the same image retain caches. Cleanup commits before the image
// marker; on failure cache access is disabled and retries at the next boot.
// Wi-Fi, clock, reminder preferences, cardid and Recovery are preserved.
esp_err_t pdkpass_cache_init(void);
// Read/write only the private partition; never import old default-NVS data.
esp_err_t pdkpass_cache_read_blob(const char *ns, const char *key,
                                 void *data, size_t *size);
esp_err_t pdkpass_cache_write_blob(const char *ns, const char *key,
                                  const void *data, size_t size);
void pdkpass_cache_forget_namespace(const char *ns);
