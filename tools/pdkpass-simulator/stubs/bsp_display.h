#pragma once

#include <stdint.h>
#include <stdbool.h>

void bsp_display_backlight(uint8_t percent);
void bsp_lvgl_set_drawing(bool enabled);
