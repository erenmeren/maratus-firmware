#pragma once
#include <stddef.h>

// Build the per-device MQTT topics into `out` (NUL-terminated, truncated to cap).
// device_id is the cloud deviceId (== config mqtt.username).
void mqtt_topic_cmd(char *out, size_t cap, const char *device_id);   // d/{id}/cmd  (subscribe)
void mqtt_topic_ack(char *out, size_t cap, const char *device_id);   // d/{id}/ack  (publish)
void mqtt_topic_hb(char *out, size_t cap, const char *device_id);    // d/{id}/hb   (publish)
