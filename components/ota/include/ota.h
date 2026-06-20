#pragma once
#include <stdbool.h>
// Check the cloud manifest; if a different version (or forced), download+install via
// esp_https_ota and reboot. No-op if no manifest / same version / not forced.
void ota_check_and_update(bool forced);
// If the running image is pending-verify (just OTA'd), mark it valid so the bootloader
// won't roll back. Call after a healthy cloud check-in.
void ota_mark_valid_if_pending(void);
