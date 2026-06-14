#pragma once
#include <stddef.h>
#include <stdint.h>

// The bundled test receipt PNG (M3). Returns a pointer to the embedded bytes
// and writes the length to *len. Replaced by the live ESC/POS renderer in M4.
const uint8_t *assets_test_png(size_t *len);
