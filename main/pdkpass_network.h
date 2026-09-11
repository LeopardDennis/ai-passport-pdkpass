#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    PDKPASS_NETWORK_STARTING = 0,
    PDKPASS_NETWORK_SETUP,
    PDKPASS_NETWORK_CONNECTING,
    PDKPASS_NETWORK_SYNCING,
    PDKPASS_NETWORK_ONLINE,
    PDKPASS_NETWORK_OFFLINE,
    PDKPASS_NETWORK_TIME_ERROR,
} pdkpass_network_state_t;

typedef struct {
    pdkpass_network_state_t state;
    bool time_valid;
    const char *setup_ssid;
    const char *setup_password;
} pdkpass_network_update_t;

// Updates are delivered from the networking worker task. The callback must not
// retain setup string pointers and must avoid blocking network progress.
typedef void (*pdkpass_network_callback_t)(const pdkpass_network_update_t *update);

// Start the long-lived Wi-Fi, provisioning, and network-time worker. On first
// use it exposes a temporary browser-based setup network; credentials are only
// committed after the station receives an IP address. Remembers up to five
// networks, trying the most recently successful first after disconnection.
esp_err_t pdkpass_network_start(pdkpass_network_callback_t callback);
