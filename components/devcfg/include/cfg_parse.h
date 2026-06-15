#pragma once
#include "device_config.h"

// Parse a DeviceConfigPayload JSON into cfg. Defensive: unknown object types are
// kept as OBJ_UNKNOWN-free (dropped), hidden objects skipped, boxes/fonts clamped.
// Returns false (and leaves cfg->valid=false) only on a JSON parse error.
bool cfg_parse_json(const char *json, device_config_t *cfg);
