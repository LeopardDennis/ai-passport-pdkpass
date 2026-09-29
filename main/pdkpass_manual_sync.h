#pragma once

#include <stdint.h>

typedef enum {
    PDKPASS_MANUAL_IDLE = 0,
    PDKPASS_MANUAL_RUNNING,
    PDKPASS_MANUAL_UPDATED,
    PDKPASS_MANUAL_UNCHANGED,
    PDKPASS_MANUAL_PARTIAL,
    PDKPASS_MANUAL_FAILED,
    PDKPASS_MANUAL_OFFLINE,
    PDKPASS_MANUAL_BUSY,
    PDKPASS_MANUAL_COOLDOWN,
    PDKPASS_MANUAL_NOT_READY,
} pdkpass_manual_state_t;

typedef struct {
    pdkpass_manual_state_t state;
    uint32_t generation;
} pdkpass_manual_status_t;
