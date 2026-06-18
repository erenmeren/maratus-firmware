#include "assets.h"

// Symbols produced by EMBED_FILES "test_receipt.png".
extern const uint8_t test_png_start[] asm("_binary_test_receipt_png_start");
extern const uint8_t test_png_end[]   asm("_binary_test_receipt_png_end");

const uint8_t *assets_test_png(size_t *len)
{
    if (len) *len = (size_t)(test_png_end - test_png_start);
    return test_png_start;
}

// Symbols produced by EMBED_FILES "fonts/ditto_sans.ttf".
// ESP-IDF derives the symbol name from the basename only: ditto_sans_ttf.
extern const uint8_t ditto_sans_ttf_start[] asm("_binary_ditto_sans_ttf_start");
extern const uint8_t ditto_sans_ttf_end[]   asm("_binary_ditto_sans_ttf_end");

const uint8_t *assets_font(size_t *len)
{
    if (len) *len = (size_t)(ditto_sans_ttf_end - ditto_sans_ttf_start);
    return ditto_sans_ttf_start;
}

// Symbols produced by EMBED_FILES "fonts/ditto_sans_bold.ttf".
extern const uint8_t ditto_sans_bold_ttf_start[] asm("_binary_ditto_sans_bold_ttf_start");
extern const uint8_t ditto_sans_bold_ttf_end[]   asm("_binary_ditto_sans_bold_ttf_end");

const uint8_t *assets_font_bold(size_t *len)
{
    if (len) *len = (size_t)(ditto_sans_bold_ttf_end - ditto_sans_bold_ttf_start);
    return ditto_sans_bold_ttf_start;
}
