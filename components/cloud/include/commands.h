#pragma once

#include <stdbool.h>
#include "command_parse.h"

// Parse a {"commands":[{"id","type"}]} body and act on each command, acking each.
// Supported: identify (blink status dot), reboot (esp_restart — does not return).
// Unknown / refresh / config-changed are acked ok=true and otherwise ignored in M2.
// Callers that touch LVGL (identify) must NOT hold the LVGL lock here — the
// handler takes it internally as needed.
void commands_handle_body(const char *json_body);

// Ack transport: HTTP passes cloud_ack_command; MQTT passes mqtt_ack.
typedef int (*ack_fn_t)(const char *command_id, bool ok);

// Dispatch one already-parsed command, acking via `ack`. Transport-independent.
void dispatch_command(const dev_command_t *cmd, ack_fn_t ack);
