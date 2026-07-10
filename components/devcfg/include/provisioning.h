#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "device_config.h"

// Pairing code: "XXXX-XXXX" (8 chars from an unambiguous alphabet + dash). Needs
// 9 bytes + NUL. `rand_byte` supplies entropy (esp_random on device; deterministic
// in tests). Writes into out (must be >= 10 bytes); returns false if out too small.
bool provisioning_make_code(char *out, size_t out_len, uint8_t (*rand_byte)(void));

// Parse the claim-poll JSON response. On a "claimed" body with a deviceKey, copies
// it into key_out (size key_len) and returns true. Returns false for "pending",
// missing key, or malformed JSON. key_out is always NUL-terminated.
bool provisioning_parse_claim(const char *json, char *key_out, size_t key_len);

// Fill cfg with a firmware-built-in setup screen (brand defaults + title, steps,
// and a pairingCode widget) for an unprovisioned device that can't fetch config.
// Sets cfg->valid = true. Other screens are left empty.
void provisioning_default_setup_config(device_config_t *cfg);

// The registry serial is the P4's own eFuse base MAC — the same MAC esptool
// prints during factory flashing. Do NOT use esp_wifi_get_mac()/ESP_MAC_WIFI_STA:
// that is the C6 radio module's MAC, which differs from the P4's and changes if
// the radio module is ever swapped. Writes 12 lowercase hex chars + NUL into
// out (must be >= 13 bytes); out[0] == '\0' if the eFuse read fails.
void provisioning_get_serial(char out[13]);
