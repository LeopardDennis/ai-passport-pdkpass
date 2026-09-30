#pragma once
#include "esp_err.h"
#include <stddef.h>
// Worker context only. Delete named application keys; never erase a partition.
esp_err_t pdkpass_storage_erase(const char *ns, const char *const *keys, size_t count);
