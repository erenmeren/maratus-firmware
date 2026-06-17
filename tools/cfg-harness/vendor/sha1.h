#pragma once
#include <stddef.h>
#include <stdint.h>

// Host-side SHA-1 for the cfg test harness. Matches the extern seam in
// components/assetcache/asset_key.c; on device, mbedtls_sha1 fills this role.
void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]);
