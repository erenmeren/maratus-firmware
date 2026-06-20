#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    bool    open;
} wifi_ap_t;

// In-place: drop empty SSIDs, dedupe by SSID keeping the strongest RSSI, sort
// descending by RSSI. Returns the new count.
int wifi_util_dedupe_sort(wifi_ap_t *aps, int n);
bool wifi_util_valid_ssid(const char *ssid);          // non-empty, <= 32 bytes
bool wifi_util_valid_password(bool open, const char *pass); // open: any; WPA2: 8..63
