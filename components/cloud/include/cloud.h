#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HTTPS client for the Ditto M1 device contract (bearer device-key auth).

// GET /api/device/commands. On success copies up to `cap`-1 bytes of the JSON
// body into `out` (NUL-terminated) and returns the HTTP status. Returns -1 on
// transport error. The poll itself is the heartbeat (server marks the device
// online + records firmwareVersion from the x-device-version header).
int cloud_get_commands(char *out, int cap);

// POST /api/device/commands/ack with { commandId, ok }. Returns HTTP status or -1.
int cloud_ack_command(const char *command_id, bool ok);

// POST /api/ingest with a PNG image (multipart field "file") + minimal metadata.
// On HTTP 201, copies the returned receipt URL into `url_out` (NUL-terminated,
// up to url_cap-1). Returns the HTTP status, or -1 on transport/allocation error.
int cloud_post_receipt(const uint8_t *png, size_t png_len, char *url_out, int url_cap);
