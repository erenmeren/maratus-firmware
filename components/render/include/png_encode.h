#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Encode an 8-bit grayscale image (row-major, `w*h` bytes, 0=black..255=white)
// into a PNG (color type 0). Allocates the output (PSRAM-preferred); caller frees
// with free(). Returns true on success and sets *out / *out_len.
bool png_encode_gray8(const uint8_t *gray, int w, int h, uint8_t **out, size_t *out_len);
