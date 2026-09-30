#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Initialize once before either service starts. Transactions serialize HTTP,
// JSON parsing and cache publication across the season and results workers.
// Never acquire this lock from a UI or button callback.
esp_err_t pdkpass_http_init(void);
void pdkpass_http_begin(void);
// Manual operation: acquire within its monotonic deadline and bound all GETs
// until end(). False means no transaction was acquired. Worker context only.
bool pdkpass_http_begin_until(int64_t deadline_us);
bool pdkpass_http_expired(void);
bool pdkpass_http_try_begin(void);
void pdkpass_http_end(void);
// Transaction owner: release TLS before storage allocations; next GET reconnects.
void pdkpass_http_release(void);
esp_err_t pdkpass_http_get(const char *url, size_t limit, char **json);
typedef bool (*pdkpass_http_item_fn)(const cJSON *item, void *context);
esp_err_t pdkpass_http_array(const char *url, pdkpass_http_item_fn item,
                             void *context);
// GETs pace each API across both workers/transactions. Manual requests wait
// for a server cooldown within the existing transaction deadline.
// Failure-only error reporting for allocations/JSON parsing after a successful GET.
// Stage names must be static, non-sensitive labels; never log response bodies.
void pdkpass_http_report_data_failure(const char *stage, esp_err_t err,
                                      size_t bytes);
