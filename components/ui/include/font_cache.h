#pragma once
#include <stddef.h>
#include "lvgl.h"

// Initialize with an in-memory TTF (e.g. assets_font()). Call once after LVGL is up.
void font_cache_init(const void *ttf, size_t len);

// Return a cached lv_tiny_ttf font rendered at `px`. LRU; never NULL after init
// (falls back to LV_FONT_DEFAULT if tiny_ttf creation fails).
const lv_font_t *font_cache_get(int px);

// Begin a new render pass. Fonts handed out after this call are pinned for the
// duration of the pass and will not be evicted (call once per screen build).
void font_cache_begin_pass(void);
