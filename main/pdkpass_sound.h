#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// Creates a small sound worker. Audio hardware is opened only on the first key.
esp_err_t pdkpass_sound_start(void);

// Non-blocking; only the latest pending key cue is kept.
void pdkpass_sound_key(bsp_btn_t button, bsp_btn_ev_t event);
