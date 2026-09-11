#include "pdkpass_wifi_profiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    pdkpass_wifi_profiles_t profiles = {.version = 1};
    assert(pdkpass_wifi_profiles_valid(&profiles));
    assert(!pdkpass_wifi_profiles_valid(NULL));
    assert(pdkpass_wifi_profile_for_attempt(0, 0) == 0);
    for (unsigned i = 0; i < 12; ++i)
        assert(pdkpass_wifi_profile_for_attempt(5, i) == (i < 10 ? i / 2 : 5));
    for (unsigned i = 0; i < 6; ++i) {
        char ssid[16];
        snprintf(ssid, sizeof(ssid), "test-network-%u", i);
        assert(pdkpass_wifi_profiles_remember(&profiles, ssid, "test-only"));
        assert(profiles.count == (i < 5 ? i + 1 : 5));
        assert(strcmp(profiles.entries[0].ssid, ssid) == 0);
        assert(pdkpass_wifi_profiles_valid(&profiles));
    }
    assert(strcmp(profiles.entries[4].ssid, "test-network-1") == 0);
    assert(pdkpass_wifi_profiles_remember(&profiles, "test-network-3", "updated-test"));
    assert(profiles.count == 5);
    assert(strcmp(profiles.entries[0].password, "updated-test") == 0);
    assert(strcmp(profiles.entries[1].ssid, "test-network-5") == 0);
    assert(pdkpass_wifi_profiles_remember(&profiles, profiles.entries[4].ssid,
                                         profiles.entries[4].password));
    assert(strcmp(profiles.entries[0].ssid, "test-network-1") == 0);
    assert(pdkpass_wifi_profiles_remember(&profiles, "open-test", ""));
    char ssid[34], password[65];
    memset(ssid, 's', sizeof(ssid)); ssid[32] = 0;
    memset(password, 'p', sizeof(password)); password[63] = 0;
    assert(pdkpass_wifi_profiles_remember(&profiles, ssid, password));
    pdkpass_wifi_profiles_t before = profiles;
    assert(!pdkpass_wifi_profiles_remember(&profiles, "", ""));
    assert(!pdkpass_wifi_profiles_remember(&profiles, "short", "bad"));
    assert(!pdkpass_wifi_profiles_remember(&profiles, NULL, ""));
    ssid[32] = 's'; ssid[33] = 0;
    assert(!pdkpass_wifi_profiles_remember(&profiles, ssid, ""));
    password[63] = 'p'; password[64] = 0;
    assert(!pdkpass_wifi_profiles_remember(&profiles, "long", password));
    assert(memcmp(&before, &profiles, sizeof(profiles)) == 0);
    profiles.version = 2;
    assert(!pdkpass_wifi_profiles_valid(&profiles));
    profiles = before; profiles.count = 6;
    assert(!pdkpass_wifi_profiles_valid(&profiles));
    profiles = before; profiles.entries[1] = profiles.entries[0];
    assert(!pdkpass_wifi_profiles_valid(&profiles));
    profiles = before; memset(profiles.entries[0].ssid, 's', 33);
    assert(!pdkpass_wifi_profiles_valid(&profiles));
    profiles = before; memset(profiles.entries[0].password, 'p', 65);
    assert(!pdkpass_wifi_profiles_valid(&profiles));
    puts("Wi-Fi profile tests passed");
    return 0;
}
