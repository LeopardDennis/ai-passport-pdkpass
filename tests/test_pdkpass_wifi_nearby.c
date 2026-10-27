#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "pdkpass_wifi_nearby.h"

static bool add(pdkpass_wifi_nearby_t *nearby, const char *ssid, int8_t rssi)
{
    return pdkpass_wifi_nearby_add(nearby, (const uint8_t *)ssid,
                                   strlen(ssid), rssi);
}

int main(void)
{
    pdkpass_wifi_nearby_t nearby;
    pdkpass_wifi_nearby_reset(&nearby);
    assert(add(&nearby, "Office_2.4G", -70));
    assert(add(&nearby, "Home_2.4G", -48));
    assert(add(&nearby, "Office_2.4G", -40));
    assert(nearby.count == 2);
    assert(strcmp(nearby.entries[0].ssid, "Office_2.4G") == 0);
    assert(strcmp(nearby.entries[1].ssid, "Home_2.4G") == 0);

    assert(add(&nearby, "Cafe", -60));
    assert(add(&nearby, "Guest", -65));
    assert(add(&nearby, "Lab", -80));
    assert(add(&nearby, "Far away", -90));
    assert(nearby.count == 6);
    assert(strcmp(nearby.entries[4].ssid, "Lab") == 0);
    assert(add(&nearby, "New", -55));
    assert(strcmp(nearby.entries[2].ssid, "New") == 0);
    assert(strcmp(nearby.entries[4].ssid, "Guest") == 0);

    // Keep the strongest twelve distinct SSIDs, even when more APs are scanned.
    pdkpass_wifi_nearby_reset(&nearby);
    for (int i = 0; i < 20; i++) {
        char name[33];
        snprintf(name, sizeof(name), "Network-%02d", i);
        assert(add(&nearby, name, (int8_t)(-90 + i)));
    }
    assert(nearby.count == 12);
    assert(strcmp(nearby.entries[0].ssid, "Network-19") == 0);
    assert(strcmp(nearby.entries[11].ssid, "Network-08") == 0);
    assert(add(&nearby, "Network-08", -20));
    assert(nearby.count == 12 && strcmp(nearby.entries[0].ssid, "Network-08") == 0);

    assert(!add(&nearby, "Bad\nName", -10));
    const uint8_t invalid[] = {0xC0, 0xAF};
    assert(!pdkpass_wifi_nearby_add(&nearby, invalid, sizeof(invalid), -10));

    pdkpass_wifi_nearby_reset(&nearby);
    assert(add(&nearby, "Cafe\"\\WiFi", -40));
    assert(add(&nearby, "家中网络", -50));
    char json[PDKPASS_WIFI_NEARBY_JSON_CAPACITY];
    assert(pdkpass_wifi_nearby_json(&nearby, false, json, sizeof(json)));
    assert(strcmp(json, "{\"scanFailed\":false,\"networks\":[\"Cafe\\\"\\\\WiFi\",\"家中网络\"]}") == 0);
    assert(!pdkpass_wifi_nearby_json(&nearby, false, json, 12));
    pdkpass_wifi_nearby_reset(&nearby);
    assert(pdkpass_wifi_nearby_json(&nearby, true, json, sizeof(json)));
    assert(strcmp(json, "{\"scanFailed\":true,\"networks\":[]}") == 0);
    // Maximum-length SSIDs made of quotes/backslashes must all fit the HTTP buffer.
    for (unsigned i = 0; i < PDKPASS_WIFI_NEARBY_LIMIT; i++) {
        char name[33];
        memset(name, '"', 32);
        for (unsigned bit = 0; bit < 4; bit++)
            if (i & (1U << bit)) name[bit] = '\\';
        name[32] = '\0';
        assert(add(&nearby, name, (int8_t)(-30 - (int)i)));
    }
    assert(nearby.count == PDKPASS_WIFI_NEARBY_LIMIT);
    assert(pdkpass_wifi_nearby_json(&nearby, false, json, sizeof(json)));
    assert(strlen(json) > 800 && strlen(json) < sizeof(json));
    assert(strncmp(json, "{\"scanFailed\":false,\"networks\":[", strlen("{\"scanFailed\":false,\"networks\":[")) == 0);
    assert(strcmp(json + strlen(json) - 2, "]}") == 0);
    return 0;
}
