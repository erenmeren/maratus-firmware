#pragma once
#include <stdbool.h>

// Extract an MQTT command envelope's `payload` object as JSON text.
//
// A command arrives as {"commandId":…,"type":…,"action":…,"payload":{…}}. The
// two payload-carrying types both hand their payload to a parser that already
// takes JSON text — cfg_parse_json for config, ota_parse_manifest for the
// firmware manifest — so this is the one step they share.
//
// On success *out is a cJSON_PrintUnformatted string owned by the caller, freed
// with cJSON_free. Returns false and leaves *out untouched when the envelope is
// malformed, is not an object, or has no OBJECT payload — which includes the
// legacy `payload: null` nudge. A false return on a config command means "ask
// again over cfg/get", not "the config was broken".
bool cmd_payload_json(const char *envelope, char **out);
