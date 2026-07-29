#pragma once
#include <stdbool.h>
#include "device_config.h"
#include "command_parse.h"   // dev_command_t

// Start the MQTT client from a config that has an enabled mqtt block. Safe to
// call when already running (no-op if the same host/username). Returns false if
// cfg->mqtt.enabled is false or the device key is missing.
bool mqtt_start(const device_config_t *cfg);
void mqtt_stop(void);
bool mqtt_is_connected(void);

// Non-blocking: pop the next MQTT-delivered command into `out`. True if one was
// returned. Drain in a while-loop from poll_task after a wake.
bool mqtt_poll_command(dev_command_t *out);

// Registered by main; invoked (on the esp-mqtt task) after a command is queued,
// so poll_task can be woken to drain it. Keep the cb tiny (a task notify).
void mqtt_set_wake_cb(void (*cb)(void));

// True if a full config payload has been reassembled and is waiting to be
// parsed (set when a "config-changed" command's payload arrived staged across
// multiple MQTT fragments). Poll from poll_task; cheap (a bool read).
bool mqtt_config_pending(void);

// Parse the staged config JSON into `out` under the staging mutex and clear
// the pending flag. False if nothing is staged, or the staged envelope has no
// object payload (e.g. the legacy `payload: null` nudge — re-request over
// cfg/get), or the payload failed to parse.
bool mqtt_parse_pending_config(device_config_t *out);

// Publish helpers. mqtt_ack matches ack_fn_t. No-ops when not connected.
int  mqtt_ack(const char *command_id, bool ok);
// `fonts` = font_cache_used(). Image diagnostics: `afetch` = cloud_last_asset_status(),
// `aimg` = ui_last_image_state(), `cfgimg` = ui_live_image_count(), `cfgstat` =
// cloud_last_config_status(), `cfgparse` = cloud_last_parse_ok(). Pass <-999 to omit.
void mqtt_publish_hb(const char *version, int fonts, int afetch, int aimg,
                     int cfgimg, int cfgstat, int cfgparse);

// Ask the cloud to republish the full config (on the existing cmd topic) by
// publishing an empty object to d/{id}/cfg/get. No-op when not connected.
// Call once per fresh MQTT connection — never on a timer/poll.
void mqtt_publish_cfg_get(void);
