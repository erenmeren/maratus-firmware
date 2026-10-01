#include "assets.h"

// Symbols produced by EMBED_FILES "fonts/maratus_sans.ttf".
// ESP-IDF derives the symbol name from the basename only: maratus_sans_ttf.
extern const uint8_t maratus_sans_ttf_start[] asm("_binary_maratus_sans_ttf_start");
extern const uint8_t maratus_sans_ttf_end[]   asm("_binary_maratus_sans_ttf_end");

const uint8_t *assets_font(size_t *len)
{
    if (len) *len = (size_t)(maratus_sans_ttf_end - maratus_sans_ttf_start);
    return maratus_sans_ttf_start;
}

// Symbols produced by EMBED_FILES "fonts/maratus_sans_bold.ttf".
extern const uint8_t maratus_sans_bold_ttf_start[] asm("_binary_maratus_sans_bold_ttf_start");
extern const uint8_t maratus_sans_bold_ttf_end[]   asm("_binary_maratus_sans_bold_ttf_end");

const uint8_t *assets_font_bold(size_t *len)
{
    if (len) *len = (size_t)(maratus_sans_bold_ttf_end - maratus_sans_bold_ttf_start);
    return maratus_sans_bold_ttf_start;
}
