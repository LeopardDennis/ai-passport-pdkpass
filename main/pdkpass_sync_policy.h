#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { PDKPASS_SYNC_SEASON, PDKPASS_SYNC_RESULTS, PDKPASS_SYNC_COUNT } pdkpass_sync_service_t;
typedef struct { int64_t due_ms[PDKPASS_SYNC_COUNT]; } pdkpass_sync_policy_t;

uint32_t pdkpass_sync_policy_wait(const pdkpass_sync_policy_t *policy,
                                 pdkpass_sync_service_t service, int64_t now_ms);
bool pdkpass_sync_policy_idle(const pdkpass_sync_policy_t *policy, int64_t now_ms);

// Runtime scheduler. Deadlines use monotonic time, independently of SNTP jumps.
void pdkpass_sync_plan(pdkpass_sync_service_t service, uint32_t delay_ms);
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t service);
bool pdkpass_sync_idle(void);

// Persist one successful check date per data service. A day-level stamp avoids
// repeatedly writing NVS during session backfill; zero means not yet tracked.
void pdkpass_sync_status_init(void (*callback)(void));
void pdkpass_sync_mark_success(pdkpass_sync_service_t service, int64_t utc);
int64_t pdkpass_sync_last_success(pdkpass_sync_service_t service);
