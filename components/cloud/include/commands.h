#pragma once

#include <stdbool.h>
#include "command_parse.h"

// Ack transport: MQTT passes mqtt_ack.
typedef int (*ack_fn_t)(const char *command_id, bool ok);

// Dispatch one already-parsed command, acking via `ack`. Transport-independent.
void dispatch_command(const dev_command_t *cmd, ack_fn_t ack);
