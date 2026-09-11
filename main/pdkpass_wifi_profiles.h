#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PDKPASS_WIFI_PROFILE_LIMIT 5

// Byte-only, versioned NVS representation. Entries are ordered by last success.
typedef struct {
    char ssid[33];
    char password[65];
} pdkpass_wifi_profile_t;

typedef struct {
    uint8_t version;
    uint8_t count;
    pdkpass_wifi_profile_t entries[PDKPASS_WIFI_PROFILE_LIMIT];
} pdkpass_wifi_profiles_t;

// Validate untrusted persisted data, including termination and duplicate SSIDs.
bool pdkpass_wifi_profiles_valid(const pdkpass_wifi_profiles_t *profiles);

// Call only after successful connection. Updates an existing SSID or evicts the
// least recently successful entry when full. Invalid input leaves data intact.
bool pdkpass_wifi_profiles_remember(pdkpass_wifi_profiles_t *profiles,
                                  const char *ssid, const char *password);

// Bounded fallback order: retry each profile twice before moving to the next.
// Returns count once all profiles have been attempted.
size_t pdkpass_wifi_profile_for_attempt(size_t count, unsigned attempt);
