#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "device_config.h"

// HTTPS client for the parts of the Ditto device contract that are not MQTT:
// the one-time claim (no device key yet), presigned R2 asset fetches, and the
// NVS-backed config cache. Command polling/ack, config fetch and the firmware
// manifest fetch are MQTT-only (see components/mqtt_ditto) — there is no HTTP
// fallback by design.

// Load the last config persisted in NVS into *cfg (cfg->valid = false if none).
void cloud_config_load_cached(device_config_t *cfg);

// Persist a config PAYLOAD (not an envelope) to the NVS cache used by
// cloud_config_load_cached, so the next boot can seed from it. Written after
// an MQTT config-changed push whose payload was reassembled in PSRAM and
// parsed by cfg_parse_json already.
void cloud_config_cache_write(const char *json);

// HTTPS GET an already-presigned, absolute asset URL into a freshly allocated
// PSRAM buffer (MALLOC_CAP_SPIRAM). On ESP_OK, *out points to *out_len bytes the
// CALLER must heap_caps_free. Response is capped at 256 KB; cap-exceed, non-200,
// or transport error frees any partial buffer and returns an esp_err_t != ESP_OK.
esp_err_t cloud_fetch_asset(const char *url, uint8_t **out, size_t *out_len);

// Outcome of the most recent cloud_fetch_asset, for heartbeat diagnostics.
// -1000 never attempted · -1 transport/TLS error · -2 too large · else HTTP status.
int cloud_last_asset_status(void);

// Diagnostics for the most recent MQTT config-apply attempt (heartbeat).
// These no longer carry HTTP status codes — see cloud_note_config_apply.
// status: 200 applied this cycle · 0 nothing to apply, re-requested · -1000 none yet.
// parse_ok: 1 applied · 0 not applied · -1000 none yet.
int cloud_last_config_status(void);
int cloud_last_parse_ok(void);

// Record the outcome of the most recent MQTT config-apply attempt (called
// from app_state.c's config-apply block, right where mqtt_parse_pending_config
// returns). Pass (200, 1) when a staged config parsed and was applied; pass
// (0, 0) when there was nothing to apply (a `payload: null` nudge or a slot
// that failed to parse) and the device re-requested over cfg/get instead.
void cloud_note_config_apply(int status, int parse_ok);

// GET /api/device/claim?code=<code> (unauthenticated). On HTTP 200 parses the
// returned deviceKey into key_out (NUL-terminated, up to key_len-1) and returns
// true. Otherwise leaves key_out empty and returns false.
bool cloud_claim_poll(const char *code, char *key_out, size_t key_len);

// Register a hook invoked when a config-changed command arrives (set by main).
void cloud_set_config_changed_cb(void (*cb)(void));
// Internal: the registered hook (NULL if none). Used by commands.c.
void (*cloud_config_changed_handler(void))(void);

// Register a hook invoked when a firmware-update command arrives (set by main).
// Registered via callback (not a direct ota_* call) to avoid a cloud<->ota cycle.
void cloud_set_firmware_update_cb(void (*cb)(void));
// Internal: the registered hook (NULL if none). Used by commands.c.
void (*cloud_firmware_update_handler(void))(void);

// Trigger callback: invoked with the show_qr url when a trigger command arrives.
// Registered by the app so the cloud component stays decoupled from app/ui state.
void cloud_set_trigger_cb(void (*cb)(const char *url));
void (*cloud_trigger_handler(void))(const char *url);
