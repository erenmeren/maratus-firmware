#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "lvgl.h"

// Initialize with the in-memory regular + bold TTFs (e.g. assets_font() /
// assets_font_bold()). Call once after LVGL is up.
void font_cache_init(const void *reg_ttf, size_t reg_len,
                     const void *bold_ttf, size_t bold_len);

// Return a cached lv_tiny_ttf font rendered at `px` in the regular or bold face.
// Grow-once cache; never NULL after init (falls back to LV_FONT_DEFAULT if
// creation fails, or if the fixed slot budget is exhausted).
const lv_font_t *font_cache_get(int px, bool bold);

// No-op, retained for API compatibility with existing callers. The cache
// never evicts or destroys fonts, so there is no pass to begin or pin.
void font_cache_begin_pass(void);
