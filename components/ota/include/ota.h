#pragma once
#include <stdbool.h>
#include "ota_manifest.h"   // fw_manifest_t
// Download+install+reboot from an already-parsed manifest (no cloud fetch, no
// version gate — the caller/cloud already decided this device should update).
// No-op if `m` is NULL or missing version/url. Used by the MQTT firmware-update
// path, where the manifest arrives in the command payload (the OTA binary
// itself is still fetched over HTTPS via esp_https_ota below).
void ota_start_with_manifest(const fw_manifest_t *m);
// If the running image is pending-verify (just OTA'd), mark it valid so the bootloader
// won't roll back. Call after a healthy cloud check-in.
void ota_mark_valid_if_pending(void);
