#pragma once
#include <stdbool.h>
#include "ota_manifest.h"   // fw_manifest_t
// Check the cloud manifest; if a different version (or forced), download+install via
// esp_https_ota and reboot. No-op if no manifest / same version / not forced.
void ota_check_and_update(bool forced);
// Download+install+reboot from an already-parsed manifest (no cloud fetch, no
// version gate — the caller/cloud already decided this device should update).
// No-op if `m` is NULL or missing version/url. Used by the MQTT firmware-update
// path, where the manifest arrives in the command payload instead of being
// polled via GET /api/device/firmware.
void ota_start_with_manifest(const fw_manifest_t *m);
// If the running image is pending-verify (just OTA'd), mark it valid so the bootloader
// won't roll back. Call after a healthy cloud check-in.
void ota_mark_valid_if_pending(void);
