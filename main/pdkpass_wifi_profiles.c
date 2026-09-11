#include "pdkpass_wifi_profiles.h"

#include <string.h>

static bool profile_valid(const pdkpass_wifi_profile_t *profile)
{
    const char *ssid_end = memchr(profile->ssid, 0, sizeof(profile->ssid));
    const char *password_end = memchr(profile->password, 0, sizeof(profile->password));
    if (!ssid_end || ssid_end == profile->ssid || !password_end) return false;
    size_t length = (size_t)(password_end - profile->password);
    return length == 0 || (length >= 8 && length <= 63);
}

bool pdkpass_wifi_profiles_valid(const pdkpass_wifi_profiles_t *profiles)
{
    if (!profiles || profiles->version != 1 ||
        profiles->count > PDKPASS_WIFI_PROFILE_LIMIT) return false;
    for (size_t i = 0; i < profiles->count; ++i) {
        if (!profile_valid(&profiles->entries[i])) return false;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(profiles->entries[i].ssid, profiles->entries[j].ssid) == 0)
                return false;
        }
    }
    return true;
}

bool pdkpass_wifi_profiles_remember(pdkpass_wifi_profiles_t *profiles,
                                  const char *ssid, const char *password)
{
    if (!pdkpass_wifi_profiles_valid(profiles) || !ssid || !password) return false;
    // Inputs are caller-owned terminated strings; copy before rearranging so
    // callers may pass pointers into the existing list.
    size_t ssid_len = strlen(ssid), password_len = strlen(password);
    if (!ssid_len || ssid_len > 32 || password_len > 63 ||
        (password_len && password_len < 8)) return false;
    pdkpass_wifi_profile_t entry = {0};
    memcpy(entry.ssid, ssid, ssid_len);
    memcpy(entry.password, password, password_len);
    size_t index = 0;
    while (index < profiles->count &&
           strcmp(profiles->entries[index].ssid, ssid) != 0) ++index;
    if (index == profiles->count) {
        if (profiles->count < PDKPASS_WIFI_PROFILE_LIMIT) ++profiles->count;
        index = profiles->count - 1;
    }
    memmove(&profiles->entries[1], &profiles->entries[0],
            index * sizeof(entry));
    profiles->entries[0] = entry;
    return true;
}

size_t pdkpass_wifi_profile_for_attempt(size_t count, unsigned attempt)
{
    size_t index = attempt / 2U;
    return index < count ? index : count;
}
