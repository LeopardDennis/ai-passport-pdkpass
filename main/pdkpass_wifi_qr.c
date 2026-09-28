#include "pdkpass_wifi_qr.h"

static bool append_char(char *output, size_t capacity, size_t *length, char value)
{
    if (*length + 1 >= capacity) return false;
    output[(*length)++] = value;
    output[*length] = '\0';
    return true;
}

static bool append_text(char *output, size_t capacity, size_t *length,
                        const char *text, bool escape)
{
    for (const char *p = text; *p; p++) {
        if (escape && (*p == '\\' || *p == ';' || *p == ',' || *p == ':') &&
            !append_char(output, capacity, length, '\\')) return false;
        if (!append_char(output, capacity, length, *p)) return false;
    }
    return true;
}

bool pdkpass_wifi_qr_payload(char *output, size_t capacity,
                             const char *ssid, const char *password)
{
    if (!output || capacity == 0) return false;
    output[0] = '\0';
    if (!ssid || !ssid[0] || !password || !password[0]) return false;
    size_t length = 0;
    return append_text(output, capacity, &length, "WIFI:T:WPA;S:", false) &&
           append_text(output, capacity, &length, ssid, true) &&
           append_text(output, capacity, &length, ";P:", false) &&
           append_text(output, capacity, &length, password, true) &&
           append_text(output, capacity, &length, ";;", false);
}
