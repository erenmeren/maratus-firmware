#pragma once
#include <stddef.h>
#include <stdint.h>

// The bundled test receipt PNG (M3). Returns a pointer to the embedded bytes
// and writes the length to *len. Replaced by the live ESC/POS renderer in M4.
const uint8_t *assets_test_png(size_t *len);

// The embedded Latin-subset TTF (Open Sans Regular, OFL-1.1).
// Returns a pointer to the raw TTF bytes and writes the length to *len.
const uint8_t *assets_font(size_t *len);

// The embedded Latin-subset TTF (Open Sans Bold, OFL-1.1) — same codepoint set as
// the regular face. Used for emphasized text (e.g. the clock time) to match the
// branding-page preview. Returns the raw TTF bytes; writes the length to *len.
const uint8_t *assets_font_bold(size_t *len);
