#pragma once

#include <stdint.h>
#include "esp_err.h"
#include <stdbool.h>

void bsp_display_backlight(uint8_t percent);
void bsp_lvgl_set_drawing(bool enabled);

esp_err_t bsp_display_sleep(bool sleep);
