#include "wifi_util.h"
#include <string.h>

bool wifi_util_valid_ssid(const char *ssid) {
    if (!ssid) return false;
    size_t n = strlen(ssid);
    return n >= 1 && n <= 32;
}

bool wifi_util_valid_password(bool open, const char *pass) {
    if (open) return true;
    if (!pass) return false;
    size_t n = strlen(pass);
    return n >= 8 && n <= 63;
}

int wifi_util_dedupe_sort(wifi_ap_t *aps, int n) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (aps[i].ssid[0] != '\0') aps[m++] = aps[i];
    }
    for (int i = 0; i < m; i++) {
        for (int j = i + 1; j < m; ) {
            if (strcmp(aps[i].ssid, aps[j].ssid) == 0) {
                if (aps[j].rssi > aps[i].rssi) aps[i] = aps[j];
                aps[j] = aps[--m];
            } else {
                j++;
            }
        }
    }
    for (int i = 1; i < m; i++) {
        wifi_ap_t key = aps[i];
        int j = i - 1;
        while (j >= 0 && aps[j].rssi < key.rssi) { aps[j + 1] = aps[j]; j--; }
        aps[j + 1] = key;
    }
    return m;
}
