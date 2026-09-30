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
    PDKPASS_MANUAL_TIMED_OUT,
} pdkpass_manual_state_t;

typedef struct {
    pdkpass_manual_state_t state;
    uint32_t generation;
    int64_t deadline_us; // Monotonic, includes connecting and waiting for HTTP work.
    int64_t cooldown_until_us; // Monotonic retry eligibility.
    unsigned session; // Selected results session; unused for points refresh.
} pdkpass_manual_status_t;

#define PDKPASS_MANUAL_TIMEOUT_US (120LL * 1000000LL)

// Poll connectivity in worker context with the same overall deadline as HTTP.
// Network callbacks can wake the worker sooner. Zero means connection timed out.
static inline uint32_t pdkpass_manual_connect_wait_ms(int64_t deadline_us, int64_t now_us)
{
    if (now_us >= deadline_us) return 0;
    int64_t remaining_ms = (deadline_us - now_us + 999) / 1000;
    return remaining_ms > 1000 ? 1000U : (uint32_t)remaining_ms;
}

// Project a deadline into UI feedback without releasing worker ownership.
// A new request stays BUSY until the old worker has stopped and cleaned up.
static inline pdkpass_manual_status_t pdkpass_manual_visible_status(
    pdkpass_manual_status_t status, int64_t now_us)
{
    if (status.state == PDKPASS_MANUAL_RUNNING && status.deadline_us > 0 &&
        now_us >= status.deadline_us) {
        status.state = PDKPASS_MANUAL_TIMED_OUT;
        status.generation++;
    }
    return status;
}
