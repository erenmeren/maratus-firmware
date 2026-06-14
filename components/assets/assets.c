#include "assets.h"

// Symbols produced by EMBED_FILES "test_receipt.png".
extern const uint8_t test_png_start[] asm("_binary_test_receipt_png_start");
extern const uint8_t test_png_end[]   asm("_binary_test_receipt_png_end");

const uint8_t *assets_test_png(size_t *len)
{
    if (len) *len = (size_t)(test_png_end - test_png_start);
    return test_png_start;
}
