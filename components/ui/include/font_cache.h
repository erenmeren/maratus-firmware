#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "lvgl.h"

// Initialize with the in-memory regular + bold TTFs (e.g. assets_font() /
// assets_font_bold()). Call once after LVGL is up.
void font_cache_init(const void *reg_ttf, size_t reg_len,
                     const void *bold_ttf, size_t bold_len);

// Return a cached lv_tiny_ttf font rendered at `px` in the regular or bold face.
// LRU; never NULL after init (falls back to LV_FONT_DEFAULT if creation fails).
const lv_font_t *font_cache_get(int px, bool bold);

// Begin a new render pass. Fonts handed out after this call are pinned for the
// duration of the pass and will not be evicted (call once per screen build).
void font_cache_begin_pass(void);
