#include "mqtt_topics.h"
#include <stdio.h>

void mqtt_topic_cmd(char *out, size_t cap, const char *device_id) { snprintf(out, cap, "d/%s/cmd", device_id ? device_id : ""); }
void mqtt_topic_ack(char *out, size_t cap, const char *device_id) { snprintf(out, cap, "d/%s/ack", device_id ? device_id : ""); }
void mqtt_topic_hb(char *out, size_t cap, const char *device_id)  { snprintf(out, cap, "d/%s/hb",  device_id ? device_id : ""); }
