// Device-side implementation of the sha1_hash seam declared extern in
// asset_key.c. The host test harness vendors a public-domain SHA-1
// (tools/cfg-harness/vendor/sha1.c); on device we route to mbedtls so both
// sides produce identical digests for the same bytes.

#include <stddef.h>
#include <stdint.h>
#include "mbedtls/sha1.h"

void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20])
{
    // Single-shot SHA-1 (mbedtls 3.x; returns 0 on success). If the digest
    // ever failed we leave out[] as-is — callers only hash bounded URL bytes
    // so this does not fail in practice.
    (void)mbedtls_sha1(data, len, out);
}
