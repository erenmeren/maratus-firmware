#pragma once

#include <stdbool.h>

// HTTPS client for the Ditto M1 device contract (bearer device-key auth).

// GET /api/device/commands. On success copies up to `cap`-1 bytes of the JSON
// body into `out` (NUL-terminated) and returns the HTTP status. Returns -1 on
// transport error. The poll itself is the heartbeat (server marks the device
// online + records firmwareVersion from the x-device-version header).
int cloud_get_commands(char *out, int cap);
