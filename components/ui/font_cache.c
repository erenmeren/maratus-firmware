#include "font_cache.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"

// Two faces (regular + bold), so the cache holds (px, bold) entries. Bumped from 8
// to leave headroom now that a single screen can mix both faces (e.g. the clock's
// bold time + regular date) alongside other text sizes.
#define FONT_CACHE_MAX 12

static const void *s_reg;  static size_t s_reg_len;
static const void *s_bold; static size_t s_bold_len;

typedef struct { int px; bool bold; lv_font_t *font; uint32_t lru; uint32_t pass; } slot_t;
static slot_t s_slots[FONT_CACHE_MAX];
static uint32_t s_tick;
static uint32_t s_pass;

void font_cache_init(const void *reg_ttf, size_t reg_len,
                     const void *bold_ttf, size_t bold_len) {
    s_reg = reg_ttf;  s_reg_len = reg_len;
    s_bold = bold_ttf; s_bold_len = bold_len;
    s_pass = 0;
    for (int i = 0; i < FONT_CACHE_MAX; i++) { s_slots[i].px = 0; s_slots[i].font = NULL; s_slots[i].pass = 0; }
}

void font_cache_begin_pass(void) { s_pass++; }

const lv_font_t *font_cache_get(int px, bool bold) {
    if (px < 8) px = 8;
    if (px > 160) px = 160;
    s_tick++;
    int free_i = -1, lru_i = -1;
    for (int i = 0; i < FONT_CACHE_MAX; i++) {
        if (s_slots[i].font && s_slots[i].px == px && s_slots[i].bold == bold) {
            s_slots[i].lru = s_tick; s_slots[i].pass = s_pass;
            return s_slots[i].font;
        }
        if (!s_slots[i].font && free_i < 0) free_i = i;
        if (s_slots[i].font && s_slots[i].pass != s_pass &&
            (lru_i < 0 || s_slots[i].lru < s_slots[lru_i].lru)) lru_i = i;
    }
    int slot = (free_i >= 0) ? free_i : lru_i;
    if (slot < 0) return LV_FONT_DEFAULT;   // all slots pinned this pass — don't evict a live font
    if (s_slots[slot].font) { lv_tiny_ttf_destroy(s_slots[slot].font); s_slots[slot].font = NULL; }
    const void *src = bold ? s_bold : s_reg;
    size_t      len = bold ? s_bold_len : s_reg_len;
    lv_font_t *f = src ? lv_tiny_ttf_create_data(src, len, px) : NULL;
    if (!f) return LV_FONT_DEFAULT;
    s_slots[slot].px = px; s_slots[slot].bold = bold; s_slots[slot].font = f;
    s_slots[slot].lru = s_tick; s_slots[slot].pass = s_pass;
    return f;
}
