#pragma once

// Runtime configuration. In M2 these come from Kconfig; M6 sources the device
// key from NVS (after on-screen claim) instead.
const char *appcfg_base_url(void);    // e.g. "https://...vercel.app" (no trailing slash)
const char *appcfg_device_key(void);  // raw bearer key
const char *appcfg_fw_version(void);  // x-device-version value
