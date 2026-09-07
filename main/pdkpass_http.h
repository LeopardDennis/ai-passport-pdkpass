#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>

// Initialize once before either service starts. Transactions serialize HTTP,
// JSON parsing and cache publication across the season and results workers.
// Never acquire this lock from a UI or button callback.
esp_err_t pdkpass_http_init(void);
void pdkpass_http_begin(void);
void pdkpass_http_end(void);
esp_err_t pdkpass_http_get(const char *url, size_t limit, char **json);
typedef bool (*pdkpass_http_item_fn)(const cJSON *item, void *context);
esp_err_t pdkpass_http_array(const char *url, pdkpass_http_item_fn item,
                             void *context);
