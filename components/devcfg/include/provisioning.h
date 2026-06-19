#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Pairing code: "XXXX-XXXX" (8 chars from an unambiguous alphabet + dash). Needs
// 9 bytes + NUL. `rand_byte` supplies entropy (esp_random on device; deterministic
// in tests). Writes into out (must be >= 10 bytes); returns false if out too small.
bool provisioning_make_code(char *out, size_t out_len, uint8_t (*rand_byte)(void));

// Parse the claim-poll JSON response. On a "claimed" body with a deviceKey, copies
// it into key_out (size key_len) and returns true. Returns false for "pending",
// missing key, or malformed JSON. key_out is always NUL-terminated.
bool provisioning_parse_claim(const char *json, char *key_out, size_t key_len);
