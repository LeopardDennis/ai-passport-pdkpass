#include "pdkpass_ui_updates.h"
#include <string.h>

static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t n = 0;
    if (text) while (n + 1 < capacity && text[n]) { out[n] = text[n]; ++n; }
    out[n] = '\0';
}

void pdkpass_ui_updates_network(pdkpass_ui_updates_t *pending,
                                const pdkpass_network_update_t *update)
{
    pending->update = *update;
    copy_text(pending->ssid, sizeof(pending->ssid), update->setup_ssid);
    copy_text(pending->password, sizeof(pending->password), update->setup_password);
    copy_text(pending->error, sizeof(pending->error), update->setup_error);
    // Pointers are bound only in the consumer's private copy.
    pending->update.setup_ssid = NULL;
    pending->update.setup_password = NULL;
    pending->update.setup_error = NULL;
    pending->network = true;
}

bool pdkpass_ui_updates_pending(const pdkpass_ui_updates_t *pending)
{
    return pending->network || pending->season || pending->status || pending->races;
}

void pdkpass_ui_updates_take(pdkpass_ui_updates_t *pending,
                             pdkpass_ui_updates_t *out)
{
    *out = *pending;
    out->update.setup_ssid = out->ssid;
    out->update.setup_password = out->password;
    out->update.setup_error = out->error;
    pending->network = pending->season = pending->status = false;
    pending->races = 0;
}
