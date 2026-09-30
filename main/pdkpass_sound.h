#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include <stdbool.h>

// Prepares audio on a worker at startup; closes it after 250 ms idle.
// Failed init/open attempts retry on a new cue after at least 60 seconds.
esp_err_t pdkpass_sound_start(void);

// Non-blocking button callback entry: PRESS cues immediately; long OK adds
// a back cue. CLICK/DOUBLE are ignored. Only the latest pending cue is kept.
void pdkpass_sound_key(bsp_btn_t button, bsp_btn_ev_t event);

// Non-blocking cue for a newly persisted result. Reminders take priority.
void pdkpass_sound_result_ready(void);

// Non-blocking: the existing audio worker streams the approved melody at 80%.
void pdkpass_sound_reminder_play(void);
void pdkpass_sound_reminder_stop(void);
// Button callback only. Consume the dismissing PRESS and its later click/long
// events so dismissing a reminder never navigates or produces a key cue.
bool pdkpass_sound_consume_key(bsp_btn_t button, bsp_btn_ev_t event);
