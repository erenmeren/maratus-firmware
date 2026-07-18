#include "font_cache.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include "esp_log.h"

// Grow-once, never-destroy cache. Fonts handed out by font_cache_get() are
// never freed for the lifetime of the app. This looks wasteful but isn't
// optional: a screen from an older render pass can still be alive (e.g. the
// branding screen hidden underneath the Settings overlay keeps its clock and
// Wi-Fi RSSI widget timers running and touching labels). Any of those live
// labels can hold a pointer into a font this cache handed out, and there is
// no pass-based bookkeeping that can prove a given font is unreferenced —
// only that it wasn't used in the *current* pass. Destroying an
// out-of-pass-but-still-live font on eviction caused a jump-through-freed
// pointer panic (root-caused 2026-07-11 via serial backtrace through
// clock_tick_cb -> lv_font_get_glyph_dsc). If the fixed slot budget below is
// ever exhausted we degrade to LV_FONT_DEFAULT instead of destroying
// anything.

// Two faces (regular + bold), so the cache holds (px, bold) entries.
// 32 because slots are never reclaimed: the union of every distinct (px,bold)
// ever requested accumulates for the whole uptime. The fixed screens (splash,
// PIN, menu, About, device settings, Wi-Fi setup) alone use ~14 combos;
// config-driven branding sizes (text objects, wordmark, clock time+date,
// countdown, pairing/steps) come on top and can change on config re-push.
// The array itself is cheap (~1 KB); real font RAM scales with sizes actually
// requested, not with this cap.
#define FONT_CACHE_MAX 32

static const char *TAG = "font_cache";

static const void *s_reg;  static size_t s_reg_len;
static const void *s_bold; static size_t s_bold_len;

typedef struct { int px; bool bold; lv_font_t *font; } slot_t;
static slot_t s_slots[FONT_CACHE_MAX];

void font_cache_init(const void *reg_ttf, size_t reg_len,
                     const void *bold_ttf, size_t bold_len) {
    s_reg = reg_ttf;  s_reg_len = reg_len;
    s_bold = bold_ttf; s_bold_len = bold_len;
    for (int i = 0; i < FONT_CACHE_MAX; i++) { s_slots[i].px = 0; s_slots[i].font = NULL; }
}

// Retained for API compatibility with existing callers (ui.c, ui_settings.c,
// ui_wifi.c). The cache no longer evicts, so there is nothing to pin.
void font_cache_begin_pass(void) {}

int font_cache_used(void) {
    int n = 0;
    for (int i = 0; i < FONT_CACHE_MAX; i++) if (s_slots[i].font) n++;
    return n;
}

const lv_font_t *font_cache_get(int px, bool bold) {
    if (px < 8) px = 8;
    if (px > 160) px = 160;
    int free_i = -1;
    for (int i = 0; i < FONT_CACHE_MAX; i++) {
        if (s_slots[i].font && s_slots[i].px == px && s_slots[i].bold == bold) {
            return s_slots[i].font;
        }
        if (!s_slots[i].font && free_i < 0) free_i = i;
    }
    if (free_i < 0) {
        ESP_LOGW(TAG, "font cache full (%d slots) — falling back to LV_FONT_DEFAULT for px=%d bold=%d",
                 FONT_CACHE_MAX, px, bold);
        return LV_FONT_DEFAULT;
    }
    const void *src = bold ? s_bold : s_reg;
    size_t      len = bold ? s_bold_len : s_reg_len;
    lv_font_t *f = src ? lv_tiny_ttf_create_data(src, len, px) : NULL;
    if (!f) return LV_FONT_DEFAULT;
    s_slots[free_i].px = px; s_slots[free_i].bold = bold; s_slots[free_i].font = f;
    return f;
}
