#pragma once
#include <stddef.h>
#include <stdio.h>

// Build the per-device MQTT topics into `out` (NUL-terminated, truncated to cap).
// device_id is the cloud deviceId (== config mqtt.username).
void mqtt_topic_cmd(char *out, size_t cap, const char *device_id);   // d/{id}/cmd  (subscribe)
void mqtt_topic_ack(char *out, size_t cap, const char *device_id);   // d/{id}/ack  (publish)
void mqtt_topic_hb(char *out, size_t cap, const char *device_id);    // d/{id}/hb   (publish)

// d/{id}/cfg/get (publish) — asks the cloud to republish the full config on
// the device's existing cmd topic. Body is always "{}"; the broker's ACL and
// the cloud webhook both key off the authenticated MQTT username, not the body.
static inline void mqtt_topic_cfg_get(char *out, size_t cap, const char *device_id)
{
    snprintf(out, cap, "d/%s/cfg/get", device_id);
}
