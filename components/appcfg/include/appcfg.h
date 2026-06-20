#pragma once

#include <stdbool.h>

// Runtime configuration. In M2 these come from Kconfig; M6 sources the device
// key from NVS (after on-screen claim) instead.
const char *appcfg_base_url(void);    // e.g. "https://...vercel.app" (no trailing slash)
const char *appcfg_device_key(void);  // raw bearer key (NVS-first, Kconfig fallback)
const char *appcfg_fw_version(void);  // x-device-version value
int appcfg_roll_width(void);          // dots (e.g. 576 = 80mm, 384 = 58mm)

// True if a device key has been persisted to NVS (i.e. the device was claimed).
bool appcfg_has_device_key(void);
// Persist a device key to NVS (no-op if NULL/empty).
void appcfg_store_device_key(const char *key);
// The provisioning pairing code: NVS-persisted, generated + stored on first call.
const char *appcfg_pairing_code(void);

// True if Wi-Fi credentials have been persisted to NVS.
bool appcfg_has_wifi_creds(void);
// Persist Wi-Fi credentials to NVS (SSID ignored if NULL/empty; pass may be empty).
void appcfg_store_wifi_creds(const char *ssid, const char *pass);
const char *appcfg_wifi_ssid(void);      // NVS → Kconfig fallback
const char *appcfg_wifi_password(void);  // NVS → Kconfig fallback
