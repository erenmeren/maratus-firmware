#pragma once

#include <stdbool.h>

// Runtime configuration. In M2 these come from Kconfig; M6 sources the device
// key from NVS (after on-screen claim) instead.
const char *appcfg_base_url(void);    // e.g. "https://...vercel.app" (no trailing slash)
const char *appcfg_device_key(void);  // raw bearer key (NVS-first, Kconfig fallback)
const char *appcfg_fw_version(void);  // reported in the MQTT heartbeat + admin device list
int appcfg_roll_width(void);          // dots (e.g. 576 = 80mm, 384 = 58mm)

// True if a device key has been persisted to NVS (i.e. the device was claimed).
bool appcfg_has_device_key(void);
// Persist a device key to NVS (no-op if NULL/empty).
void appcfg_store_device_key(const char *key);
// The provisioning pairing code: NVS-persisted, generated + stored on first call.
const char *appcfg_pairing_code(void);

// True if a device id has been persisted to NVS (cloud_fetch_identity, or a
// config push that carried an mqtt.username). No Kconfig fallback: unlike the
// device key, a device id isn't a build-time constant — it doesn't exist until
// the cloud has issued one.
bool appcfg_has_device_id(void);
// Persist the device id to NVS (no-op if NULL/empty).
void appcfg_store_device_id(const char *id);
// The cloud-assigned device id (== the MQTT client id/username). "" if none
// persisted yet.
const char *appcfg_device_id(void);

// MQTT broker coordinates. NVS-first, Kconfig fallback (mirrors appcfg_base_url)
// so a device with empty NVS still has somewhere to connect.
const char *appcfg_mqtt_host(void);
int appcfg_mqtt_port(void);
// Persist broker host+port to NVS (no-op if host is NULL/empty).
void appcfg_store_mqtt(const char *host, int port);

// True if Wi-Fi credentials have been persisted to NVS.
bool appcfg_has_wifi_creds(void);
// Persist Wi-Fi credentials to NVS (SSID ignored if NULL/empty; pass may be empty).
void appcfg_store_wifi_creds(const char *ssid, const char *pass);
const char *appcfg_wifi_ssid(void);      // NVS → Kconfig fallback
const char *appcfg_wifi_password(void);  // NVS → Kconfig fallback
