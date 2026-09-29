#include "pdkpass_wifi_nearby.h"

#include <string.h>

static bool valid_utf8_ssid(const uint8_t *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint8_t lead = text[i];
        if (lead < 0x20 || lead == 0x7F) return false;
        if (lead < 0x80) {
            i++;
            continue;
        }
        size_t extra = lead >= 0xC2 && lead <= 0xDF ? 1U :
                       lead >= 0xE0 && lead <= 0xEF ? 2U :
                       lead >= 0xF0 && lead <= 0xF4 ? 3U : 0U;
        if (!extra || i + extra >= length) return false;
        uint8_t first = text[i + 1];
        if (first < 0x80 || first > 0xBF ||
            (lead == 0xE0 && first < 0xA0) ||
            (lead == 0xED && first > 0x9F) ||
            (lead == 0xF0 && first < 0x90) ||
            (lead == 0xF4 && first > 0x8F)) return false;
        for (size_t j = 2; j <= extra; j++) {
            if (text[i + j] < 0x80 || text[i + j] > 0xBF) return false;
        }
        i += extra + 1U;
    }
    return true;
}

void pdkpass_wifi_nearby_reset(pdkpass_wifi_nearby_t *nearby)
{
    if (nearby) memset(nearby, 0, sizeof(*nearby));
}

bool pdkpass_wifi_nearby_add(pdkpass_wifi_nearby_t *nearby,
                              const uint8_t *ssid, size_t length, int8_t rssi)
{
    if (!nearby || !ssid || length == 0 || length > 32 ||
        !valid_utf8_ssid(ssid, length)) return false;

    for (size_t i = 0; i < nearby->count; i++) {
        pdkpass_wifi_nearby_entry_t *entry = &nearby->entries[i];
        if (strlen(entry->ssid) != length || memcmp(entry->ssid, ssid, length)) continue;
        if (rssi > entry->rssi) {
            entry->rssi = rssi;
            while (i > 0 && entry->rssi > nearby->entries[i - 1].rssi) {
                pdkpass_wifi_nearby_entry_t previous = nearby->entries[i - 1];
                nearby->entries[i - 1] = *entry;
                *entry = previous;
                i--;
                entry = &nearby->entries[i];
            }
        }
        return true;
    }

    if (nearby->count == PDKPASS_WIFI_NEARBY_LIMIT &&
        rssi <= nearby->entries[nearby->count - 1].rssi) return true;
    size_t position = nearby->count < PDKPASS_WIFI_NEARBY_LIMIT
                          ? nearby->count++ : nearby->count - 1;
    while (position > 0 && rssi > nearby->entries[position - 1].rssi) {
        nearby->entries[position] = nearby->entries[position - 1];
        position--;
    }
    pdkpass_wifi_nearby_entry_t *entry = &nearby->entries[position];
    memcpy(entry->ssid, ssid, length);
    entry->ssid[length] = '\0';
    entry->rssi = rssi;
    return true;
}

static bool append(char *output, size_t capacity, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (*used + length >= capacity) return false;
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = '\0';
    return true;
}

bool pdkpass_wifi_nearby_json(const pdkpass_wifi_nearby_t *nearby,
                               bool scan_failed, char *output, size_t capacity)
{
    if (!nearby || !output || !capacity) return false;
    output[0] = '\0';
    size_t used = 0;
    if (!append(output, capacity, &used, scan_failed
                ? "{\"scanFailed\":true,\"networks\":["
                : "{\"scanFailed\":false,\"networks\":[")) return false;
    for (size_t i = 0; i < nearby->count; i++) {
        if (!append(output, capacity, &used, i ? ",\"" : "\"")) return false;
        const unsigned char *p = (const unsigned char *)nearby->entries[i].ssid;
        for (; *p; p++) {
            if (*p == '\\' || *p == '"') {
                if (used + 2 >= capacity) return false;
                output[used++] = '\\';
            } else if (used + 1 >= capacity) return false;
            output[used++] = (char)*p;
            output[used] = '\0';
        }
        if (!append(output, capacity, &used, "\"")) return false;
    }
    return append(output, capacity, &used, "]}");
}
