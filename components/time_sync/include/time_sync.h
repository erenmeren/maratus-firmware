#pragma once
#include <stdbool.h>

// Start the SNTP client once (idempotent). Safe to call on every reconnect.
void time_sync_start(void);

// Apply a POSIX TZ string (e.g. "PST8PDT,M3.2.0,M11.1.0"). NULL/empty → "UTC0".
void time_sync_set_tz(const char *posix_tz);

// True once the first SNTP time update has landed.
bool time_is_synced(void);
