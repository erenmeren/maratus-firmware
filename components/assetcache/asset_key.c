#include "asset_key.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Hashing seam: the host test vendors a public-domain SHA-1 (tools/cfg-harness/
// vendor/sha1.c); Phase 2 wires mbedtls_sha1 on device. SHA-1 is standardized,
// so both sides produce identical digests for the same bytes — that is the
// whole point of keeping the hash behind this extern.
extern void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]);

void asset_key(const char *url, char out[41]) {
    if (!url || url[0] == '\0') {
        out[0] = '\0';
        return;
    }

    // Hash the URL up to (but not including) the first '?'. The query carries
    // only the rotating presign params, never the object identity.
    const char *q = strchr(url, '?');
    size_t len = q ? (size_t)(q - url) : strlen(url);

    if (len == 0) {            // url begins with '?': nothing to identify.
        out[0] = '\0';
        return;
    }

    uint8_t digest[20];
    sha1_hash((const uint8_t *)url, len, digest);

    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        out[i * 2]     = HEX[digest[i] >> 4];
        out[i * 2 + 1] = HEX[digest[i] & 0x0F];
    }
    out[40] = '\0';
}
