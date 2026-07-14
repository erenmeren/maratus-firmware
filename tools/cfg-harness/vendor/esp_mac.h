#pragma once
// Minimal host-only stand-in for ESP-IDF's esp_mac.h. provisioning.c (added in
// 65af763, "show device serial on the setup screen") includes the real ESP-IDF
// header and calls esp_efuse_mac_get_default(), which this harness's plain-cc
// build (Makefile: "pure devcfg core, no ESP-IDF") cannot see. No host test
// exercises provisioning_get_serial(), so this stub only needs to satisfy the
// compiler/linker; the on-device idf.py build picks up the real ESP-IDF header
// instead (this file only reaches the compiler via the harness's -Ivendor flag).
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL -1

static inline esp_err_t esp_efuse_mac_get_default(uint8_t mac[6]) {
    (void)mac;
    return ESP_FAIL;
}
