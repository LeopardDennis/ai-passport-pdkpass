#pragma once
#include "pdkpass_network.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Caller serializes access. Fixed storage coalesces updates without allocating
// or retaining the networking worker's borrowed strings.
typedef struct {
    bool network, season, status;
    uint32_t races;
    pdkpass_network_update_t update;
    char ssid[33], password[65], error[64];
} pdkpass_ui_updates_t;
void pdkpass_ui_updates_network(pdkpass_ui_updates_t *pending,
                                const pdkpass_network_update_t *update);
bool pdkpass_ui_updates_pending(const pdkpass_ui_updates_t *pending);
void pdkpass_ui_updates_take(pdkpass_ui_updates_t *pending,
                             pdkpass_ui_updates_t *out);
