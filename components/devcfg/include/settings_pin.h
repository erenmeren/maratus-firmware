#pragma once
#include <stdbool.h>

// Verify an on-device Settings PIN against the cloud-delivered salted hash.
// Computes sha256(salt + pin) and constant-time compares the lowercase hex to
// hash_hex. Returns false if hash_hex is empty/NULL (no PIN configured).
bool settings_pin_verify(const char *pin, const char *hash_hex, const char *salt);
