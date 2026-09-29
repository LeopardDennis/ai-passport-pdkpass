#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PDKPASS_WIFI_NEARBY_LIMIT 5

typedef struct {
    char ssid[33];
    int8_t rssi;
} pdkpass_wifi_nearby_entry_t;

typedef struct {
    pdkpass_wifi_nearby_entry_t entries[PDKPASS_WIFI_NEARBY_LIMIT];
    size_t count;
} pdkpass_wifi_nearby_t;

void pdkpass_wifi_nearby_reset(pdkpass_wifi_nearby_t *nearby);
bool pdkpass_wifi_nearby_add(pdkpass_wifi_nearby_t *nearby,
                              const uint8_t *ssid, size_t length, int8_t rssi);
bool pdkpass_wifi_nearby_json(const pdkpass_wifi_nearby_t *nearby,
                               bool scan_failed, char *output, size_t capacity);
