#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "device_config.h"
#include "ota_manifest.h"

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

// GET /api/device/config with the stored ETag (cfg->etag) as If-None-Match.
//  - 200: parses brand colors + idle text objects into *cfg (cfg->valid = true,
//         cfg->etag updated) and persists raw JSON + ETag to NVS. Returns 200.
//  - 304: leaves *cfg unchanged. Returns 304.
//  - else: returns the HTTP status or -1.
int cloud_get_config(device_config_t *cfg);

// Load the last config persisted in NVS into *cfg (cfg->valid = false if none).
void cloud_config_load_cached(device_config_t *cfg);

// HTTPS GET an already-presigned, absolute asset URL into a freshly allocated
// PSRAM buffer (MALLOC_CAP_SPIRAM). On ESP_OK, *out points to *out_len bytes the
// CALLER must heap_caps_free. Response is capped at 256 KB; cap-exceed, non-200,
// or transport error frees any partial buffer and returns an esp_err_t != ESP_OK.
esp_err_t cloud_fetch_asset(const char *url, uint8_t **out, size_t *out_len);

// GET /api/device/claim?code=<code> (unauthenticated). On HTTP 200 parses the
// returned deviceKey into key_out (NUL-terminated, up to key_len-1) and returns
// true. Otherwise leaves key_out empty and returns false.
bool cloud_claim_poll(const char *code, char *key_out, size_t key_len);

// Fetch GET /api/device/firmware (device-key). On a 200 with a valid manifest, fills
// *out and returns true. 204/empty/error → false.
bool cloud_get_firmware(fw_manifest_t *out);

// Register a hook invoked when a config-changed command arrives (set by main).
void cloud_set_config_changed_cb(void (*cb)(void));
// Internal: the registered hook (NULL if none). Used by commands.c.
void (*cloud_config_changed_handler(void))(void);

// Register a hook invoked when a firmware-update command arrives (set by main).
// Registered via callback (not a direct ota_* call) to avoid a cloud<->ota cycle.
void cloud_set_firmware_update_cb(void (*cb)(void));
// Internal: the registered hook (NULL if none). Used by commands.c.
void (*cloud_firmware_update_handler(void))(void);
