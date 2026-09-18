#pragma once
#include "pdkpass_season.h"

bool pdkpass_calendar_supported(unsigned year);
// Flash-backed fallback only; caller owns the destination. No heap or NVS.
// Unknown years return false without touching the destination.
bool pdkpass_calendar_load(unsigned year, pdkpass_season_snapshot_t *out);
