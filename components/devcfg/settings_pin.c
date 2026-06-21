#include "settings_pin.h"
#include "sha256.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

bool settings_pin_verify(const char *pin, const char *hash_hex, const char *salt) {
    if (!hash_hex || hash_hex[0] == '\0') return false;   // no PIN configured
    if (!pin) pin = "";
    if (!salt) salt = "";

    SHA256_CTX ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)salt, strlen(salt));
    sha256_update(&ctx, (const uint8_t *)pin, strlen(pin));
    sha256_final(&ctx, digest);

    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);

    // Constant-time compare over the full 64 hex chars (hash_hex assumed lowercase
    // 64-char hex from the cloud; a short/long stored hash simply won't match).
    if (strlen(hash_hex) != 64) return false;
    unsigned diff = 0;
    for (int i = 0; i < 64; i++) diff |= (unsigned)(hex[i] ^ hash_hex[i]);
    return diff == 0;
}
