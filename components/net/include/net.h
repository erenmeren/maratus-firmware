#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { char ssid[33]; int8_t rssi; bool open; } net_ap_t;

void net_init(void);                                   // bring up STA, no connect
int  net_scan(net_ap_t *out, int max);                 // blocking scan, returns count
bool net_connect(const char *ssid, const char *pass);  // blocks ~20s, true if got IP
bool net_is_connected(void);
// Same as net_connect but returns immediately; the background retry loop keeps
// trying these credentials until net_stop().
void net_connect_async(const char *ssid, const char *pass);
// Drop the association and stop the background retry loop (which otherwise
// re-issues esp_wifi_connect() after every disconnect, forever).
void net_stop(void);
