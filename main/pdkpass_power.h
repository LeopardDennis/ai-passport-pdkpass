#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t pdkpass_power_init(void);
// Display calls are serialized by LVGL; network calls by the network worker.
void pdkpass_power_display(bool active);
void pdkpass_power_network(bool active);
