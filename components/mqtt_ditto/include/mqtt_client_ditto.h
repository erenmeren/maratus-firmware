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

// Publish helpers. mqtt_ack matches ack_fn_t. No-ops when not connected.
int  mqtt_ack(const char *command_id, bool ok);
// `fonts` = font_cache_used(). Image diagnostics: `afetch` = cloud_last_asset_status(),
// `aimg` = ui_last_image_state(), `cfgimg` = ui_live_image_count(), `cfgstat` =
// cloud_last_config_status(), `cfgparse` = cloud_last_parse_ok(). Pass <-999 to omit.
void mqtt_publish_hb(const char *version, int fonts, int afetch, int aimg,
                     int cfgimg, int cfgstat, int cfgparse);
