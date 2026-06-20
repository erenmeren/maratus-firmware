#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { char ssid[33]; int8_t rssi; bool open; } net_ap_t;

void net_init(void);                                   // bring up STA, no connect
int  net_scan(net_ap_t *out, int max);                 // blocking scan, returns count
bool net_connect(const char *ssid, const char *pass);  // blocks ~20s, true if got IP
bool net_is_connected(void);
