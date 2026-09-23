#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Shared by the AP interface configuration and on-device browser instructions.
#define PDKPASS_SETUP_IP "192.168.9.1"

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
    // Only a time synchronized during this boot may drive race/year changes.
    bool time_valid;
    // Restored NVS time advances while powered on, but omits power-off time.
    bool time_estimated;
    const char *setup_ssid;
    const char *setup_password;
    const char *setup_error; // Optional short, credential-free failure message.
    unsigned setup_seconds_left;
    bool hotspot_active;
} pdkpass_network_update_t;

// Updates are delivered from the networking worker task. The callback must not
// retain setup string pointers and must avoid blocking network progress.
typedef void (*pdkpass_network_callback_t)(const pdkpass_network_update_t *update);

// Boot scans saved networks once; failures power down Wi-Fi. Setup is manual.
esp_err_t pdkpass_network_start(pdkpass_network_callback_t callback);

typedef enum {
    PDKPASS_NETWORK_RETRY,
    PDKPASS_NETWORK_OPEN_SETUP,
    PDKPASS_NETWORK_CANCEL,
    // Internal: reconnect only after an intentional power-saving disconnect.
    PDKPASS_NETWORK_SYNC,
    PDKPASS_NETWORK_POLICY,
} pdkpass_network_command_t;
// Nonblocking; safe under the UI lock. The worker owns all radio operations.
void pdkpass_network_request(pdkpass_network_command_t command);
