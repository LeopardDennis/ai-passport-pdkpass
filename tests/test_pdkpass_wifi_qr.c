#include <assert.h>
#include <string.h>

#include "pdkpass_wifi_qr.h"

int main(void)
{
    char payload[128];
    assert(pdkpass_wifi_qr_payload(payload, sizeof(payload),
                                    "PDKPASS-A7C9", "J4M7K2P9"));
    assert(strcmp(payload,
                  "WIFI:T:WPA;S:PDKPASS-A7C9;P:J4M7K2P9;;") == 0);

    assert(pdkpass_wifi_qr_payload(payload, sizeof(payload),
                                    "Pit;Lane", "a,b:c\\d123"));
    assert(strcmp(payload,
                  "WIFI:T:WPA;S:Pit\\;Lane;P:a\\,b\\:c\\\\d123;;") == 0);

    assert(!pdkpass_wifi_qr_payload(payload, 12, "PDKPASS", "password"));
    assert(!pdkpass_wifi_qr_payload(payload, sizeof(payload), "", "password"));
    assert(!pdkpass_wifi_qr_payload(payload, sizeof(payload), "PDKPASS", ""));
    assert(!pdkpass_wifi_qr_payload(NULL, sizeof(payload), "PDKPASS", "password"));
    return 0;
}
