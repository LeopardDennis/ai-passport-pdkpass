#pragma once

#include <stdbool.h>
#include <stddef.h>

// Build the standard Wi-Fi QR payload for the device's WPA2 setup hotspot.
// Returns false for missing credentials or insufficient output capacity.
bool pdkpass_wifi_qr_payload(char *output, size_t capacity,
                             const char *ssid, const char *password);
