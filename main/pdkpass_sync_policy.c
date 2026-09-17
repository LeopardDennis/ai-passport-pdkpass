#include "pdkpass_sync_policy.h"
#include <limits.h>

uint32_t pdkpass_sync_policy_wait(const pdkpass_sync_policy_t *policy,
                                 pdkpass_sync_service_t service, int64_t now_ms)
{
    if ((unsigned)service >= PDKPASS_SYNC_COUNT) return UINT32_MAX;
    int64_t remaining = policy->due_ms[service] - now_ms;
    if (remaining <= 0) return 0;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

bool pdkpass_sync_policy_idle(const pdkpass_sync_policy_t *policy, int64_t now_ms)
{
    for (unsigned i = 0; i < PDKPASS_SYNC_COUNT; i++) {
        // Keep a working connection for imminent work/backfill, rather than
        // spending more energy reconnecting between adjacent requests.
        if (pdkpass_sync_policy_wait(policy, (pdkpass_sync_service_t)i, now_ms) <= 60000U)
            return false;
    }
    return true;
}
