#include "ui.h"
#include "font_cache.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"

// On-device Settings screens (M7). PASSIVE by design: LVGL event callbacks
// ONLY set volatile flags here — no network, no esp_restart, no cloud calls.
// An orchestrator (Task 7) reads user intent via the consume_* functions and
// pushes data in via the set_* mutators, mirroring the ui_wifi.c pattern.
// Every public function takes the LVGL port lock internally, matching ui.c's
// locking discipline.
//
// VISUAL: restyled to match the dReceipt staff mockups (PinScreen / MenuScreen).
// All colors derive from the tenant brand (brand_bg/fg/accent) via lv_color_mix
// so the screens look right whether the brand bg is light or dark. The public
// API, callbacks, volatile flags and statics are unchanged so run_settings_flow()
// keeps working.

// --------------------------------------------------------------------------
// Brand-derived palette
// --------------------------------------------------------------------------
typedef struct { lv_color_t bg, fg, accent, accent_fg, card, muted; } pal_t;

static pal_t settings_palette(void) {
    const device_config_t *c = ui_current_cfg();
    uint32_t bg = (c && c->valid) ? c->brand_bg     : 0x101418;
    uint32_t fg = (c && c->valid) ? c->brand_fg     : 0xFFFFFF;
    uint32_t ac = (c && c->valid) ? c->brand_accent : 0x2F6BF0;
    pal_t p;
    p.bg        = lv_color_hex(bg);
    p.fg        = lv_color_hex(fg);
    p.accent    = lv_color_hex(ac);
    p.accent_fg = lv_color_white();
    p.card      = lv_color_mix(p.fg, p.bg, 18);    // subtle elevated surface (~7% fg)
    p.muted     = lv_color_mix(p.fg, p.bg, 140);   // mid-contrast secondary text
    return p;
}

#define SETTINGS_ERR_RED 0xE0564B

// --------------------------------------------------------------------------
// PIN keypad screen
// --------------------------------------------------------------------------

static lv_obj_t *s_pin_screen;
static lv_obj_t *s_pin_entry_lbl;   // container holding the entry dots
static lv_obj_t *s_pin_error_lbl;   // "Incorrect PIN" — hidden by default

static char s_pin_buf[16];
static volatile bool s_pin_submitted;

#define PIN_DOTS_MIN 4   // always show at least this many outline dots
#define PIN_DOTS_MAX 8   // cap

// Rebuild the dot row from s_pin_buf: filled dots (accent) for typed digits,
// outline dots for the rest. Must be called while holding the LVGL lock.
static void pin_refresh_display(void) {
    if (!s_pin_entry_lbl) return;
    lv_obj_clean(s_pin_entry_lbl);  // remove old dot children

    pal_t p = settings_palette();
    int len = (int)strlen(s_pin_buf);
    if (len > PIN_DOTS_MAX) len = PIN_DOTS_MAX;
    int count = len > PIN_DOTS_MIN ? len : PIN_DOTS_MIN;
    if (count > PIN_DOTS_MAX) count = PIN_DOTS_MAX;

    for (int i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_create(s_pin_entry_lbl);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        if (i < len) {
            lv_obj_set_style_bg_color(dot, p.accent, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(dot, p.fg, 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_opa(dot, 100, 0);  // ~40%
        }
    }
}

// --- PIN keypad event callbacks: flags/buffer only, no work ---------------
static void pin_digit_cb(lv_event_t *e) {
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    int len = (int)strlen(s_pin_buf);
    if (len < (int)(sizeof(s_pin_buf) - 1)) {
        s_pin_buf[len]     = (char)('0' + digit);
        s_pin_buf[len + 1] = '\0';
        pin_refresh_display();
    }
}

static void pin_backspace_cb(lv_event_t *e) {
    (void)e;
    int len = (int)strlen(s_pin_buf);
    if (len > 0) {
        s_pin_buf[len - 1] = '\0';
        pin_refresh_display();
    }
}

static void pin_ok_cb(lv_event_t *e) {
    (void)e;
    if (strlen(s_pin_buf) > 0) s_pin_submitted = true;
}

// Cancel latches UI_SET_CLOSE via the shared action flag (defined below).
// Forward-declared here; the definition is after the action state.
static void pin_cancel_cb(lv_event_t *e);

// --------------------------------------------------------------------------
// Menu screen
// --------------------------------------------------------------------------

static lv_obj_t *s_menu_screen;
static lv_obj_t *s_menu_info_lbl;
static lv_obj_t *s_about_screen;

static volatile ui_settings_action_t s_action;
static volatile bool                 s_action_set;

static char s_info_text[256];   // last text pushed by ui_settings_set_info

static void menu_action_cb(lv_event_t *e) {
    ui_settings_action_t act = (ui_settings_action_t)(intptr_t)lv_event_get_user_data(e);
    s_action     = act;
    s_action_set = true;
}

// Now the cancel callback can reference s_action / s_action_set.
static void pin_cancel_cb(lv_event_t *e) {
    (void)e;
    s_action     = UI_SET_CLOSE;
    s_action_set = true;
}

// --------------------------------------------------------------------------
// Small visual helpers (build-time only; no behavior)
// --------------------------------------------------------------------------

// An accent-soft rounded chip with a centered LV_SYMBOL_* icon in p.accent.
// The icon stays on LV_FONT_DEFAULT (Open Sans cache lacks symbol glyphs).
static lv_obj_t *make_icon_chip(lv_obj_t *parent, const pal_t *p,
                                int side, int radius, const char *symbol) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, side, side);
    lv_obj_set_style_radius(chip, radius, 0);
    lv_obj_set_style_bg_color(chip, p->accent, 0);
    lv_obj_set_style_bg_opa(chip, 38, 0);          // accent-soft over surface
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_label_create(chip);
    lv_label_set_text(ic, symbol);                 // default font carries symbols
    lv_obj_set_style_text_color(ic, p->accent, 0);
    lv_obj_center(ic);
    return chip;
}

// A keypad key: card bg, rounded, soft shadow, brand fg text. OK key is accent.
static lv_obj_t *make_keypad_key(lv_obj_t *parent, const pal_t *p,
                                 const char *text, bool icon_symbol, bool accent,
                                 const lv_font_t *txt_font,
                                 lv_event_cb_t cb, void *user_data) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, accent ? p->accent : p->card, 0);
    if (!accent) {
        lv_obj_set_style_shadow_width(btn, 8, 0);
        lv_obj_set_style_shadow_opa(btn, 20, 0);
        lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
        lv_obj_set_style_shadow_color(btn, p->fg, 0);
    }
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, accent ? p->accent_fg : p->fg, 0);
    if (!icon_symbol && txt_font) {
        // Open Sans for digits; default font for symbols (which carry glyphs).
        lv_obj_set_style_text_font(lbl, txt_font, LV_PART_MAIN);
    }
    lv_obj_center(lbl);
    return btn;
}

// --------------------------------------------------------------------------
// Public API — PIN screen
// --------------------------------------------------------------------------

void ui_settings_show_pin(lv_obj_t *parent) {
    lvgl_port_lock(0);

    // Reset state
    s_pin_buf[0]   = '\0';
    s_pin_submitted = false;

    // Delete any previous instance
    if (s_pin_screen) { lv_obj_delete(s_pin_screen); s_pin_screen = NULL; }

    font_cache_begin_pass();   // pin fonts for this build pass
    pal_t p = settings_palette();
    const lv_font_t *f_title  = font_cache_get(30, true);
    const lv_font_t *f_sub    = font_cache_get(17, false);
    const lv_font_t *f_key    = font_cache_get(30, true);
    const lv_font_t *f_cancel = font_cache_get(20, true);

    s_pin_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_pin_screen);
    lv_obj_set_size(s_pin_screen, 720, 720);
    lv_obj_set_pos(s_pin_screen, 0, 0);
    lv_obj_set_style_bg_color(s_pin_screen, p.bg, 0);
    lv_obj_set_style_bg_opa(s_pin_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_pin_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Settings chip (accent-soft) with a gear symbol, top-mid.
    lv_obj_t *chip = make_icon_chip(s_pin_screen, &p, 64, 32, LV_SYMBOL_SETTINGS);
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 20);

    // 2. Title
    lv_obj_t *title = lv_label_create(s_pin_screen);
    lv_label_set_text(title, "Enter PIN");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, p.fg, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 96);

    // 3a. Always-visible muted helper line.
    lv_obj_t *helper = lv_label_create(s_pin_screen);
    lv_label_set_text(helper, "Staff access");
    lv_obj_set_style_text_font(helper, f_sub, LV_PART_MAIN);
    lv_obj_set_style_text_color(helper, p.muted, 0);
    lv_obj_align(helper, LV_ALIGN_TOP_MID, 0, 140);

    // 3b. Error line (hidden by default), overlaid on the helper position.
    s_pin_error_lbl = lv_label_create(s_pin_screen);
    lv_label_set_text(s_pin_error_lbl, "Incorrect PIN");
    lv_obj_set_style_text_font(s_pin_error_lbl, f_sub, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pin_error_lbl, lv_color_hex(SETTINGS_ERR_RED), 0);
    lv_obj_align(s_pin_error_lbl, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_add_flag(s_pin_error_lbl, LV_OBJ_FLAG_HIDDEN);

    // 4. PIN dots — a flex-row container; pin_refresh_display() fills it.
    s_pin_entry_lbl = lv_obj_create(s_pin_screen);
    lv_obj_remove_style_all(s_pin_entry_lbl);
    lv_obj_set_size(s_pin_entry_lbl, LV_SIZE_CONTENT, 28);
    lv_obj_set_flex_flow(s_pin_entry_lbl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_pin_entry_lbl, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_pin_entry_lbl, 18, 0);
    lv_obj_clear_flag(s_pin_entry_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_pin_entry_lbl, LV_ALIGN_TOP_MID, 0, 180);
    pin_refresh_display();

    // 5. Keypad — 3 columns. Rows 1-3 = 1..9; row 4 = [backspace] [0] [OK].
    int key_w = 120, key_h = 78, gap_x = 16, gap_y = 16;
    int grid_w = 3 * key_w + 2 * gap_x;          // 392
    int start_x = (720 - grid_w) / 2;
    int start_y = 250;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int x = start_x + col * (key_w + gap_x);
            int y = start_y + row * (key_h + gap_y);

            lv_obj_t *btn;
            if (row == 3 && col == 0) {
                btn = make_keypad_key(s_pin_screen, &p, LV_SYMBOL_BACKSPACE,
                                      true, false, NULL, pin_backspace_cb, NULL);
            } else if (row == 3 && col == 2) {
                btn = make_keypad_key(s_pin_screen, &p, "OK",
                                      false, true, f_key, pin_ok_cb, NULL);
            } else {
                int digit = (row < 3) ? (row * 3 + col + 1) : 0;  // 1..9, then 0
                char label[2] = { (char)('0' + digit), '\0' };
                btn = make_keypad_key(s_pin_screen, &p, label,
                                      false, false, f_key, pin_digit_cb,
                                      (void *)(intptr_t)digit);
            }
            lv_obj_set_size(btn, key_w, key_h);
            lv_obj_set_pos(btn, x, y);
        }
    }

    // 6. Cancel — ghost full-width text button below the keypad.
    int cancel_y = start_y + 4 * key_h + 3 * gap_y + 8;  // just under the grid
    lv_obj_t *cancel = lv_button_create(s_pin_screen);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, grid_w, 56);
    lv_obj_set_pos(cancel, start_x, cancel_y);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cancel, pin_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, f_cancel, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_lbl, p.muted, 0);
    lv_obj_center(cancel_lbl);

    lvgl_port_unlock();
}

void ui_settings_pin_set_error(bool show) {
    lvgl_port_lock(0);
    if (s_pin_error_lbl) {
        if (show) {
            lv_obj_clear_flag(s_pin_error_lbl, LV_OBJ_FLAG_HIDDEN);
            // Clear entry so the user can re-type
            s_pin_buf[0] = '\0';
            pin_refresh_display();
        } else {
            lv_obj_add_flag(s_pin_error_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

bool ui_settings_consume_pin(char *out, int cap) {
    if (!s_pin_submitted) return false;
    s_pin_submitted = false;
    // Copy the buffer and clear it; do this under lock since pin_refresh_display
    // also touches s_pin_buf (called from LVGL task via callbacks).
    lvgl_port_lock(0);
    strncpy(out, s_pin_buf, cap - 1);
    out[cap - 1] = '\0';
    s_pin_buf[0] = '\0';
    if (s_pin_entry_lbl) pin_refresh_display();
    lvgl_port_unlock();
    return true;
}

// --------------------------------------------------------------------------
// Public API — info setter (can be called before show_menu)
// --------------------------------------------------------------------------

void ui_settings_set_info(const char *text) {
    lvgl_port_lock(0);
    strncpy(s_info_text, text ? text : "", sizeof(s_info_text) - 1);
    s_info_text[sizeof(s_info_text) - 1] = '\0';
    if (s_menu_info_lbl) lv_label_set_text(s_menu_info_lbl, s_info_text);
    lvgl_port_unlock();
}

// --------------------------------------------------------------------------
// Public API — Menu screen
// --------------------------------------------------------------------------

void ui_settings_show_menu(lv_obj_t *parent) {
    lvgl_port_lock(0);

    s_action_set = false;

    // Delete any previous instance (menu + any sub-screen overlaying it).
    if (s_menu_screen) { lv_obj_delete(s_menu_screen); s_menu_screen = NULL; }
    if (s_about_screen) { lv_obj_delete(s_about_screen); s_about_screen = NULL; }

    font_cache_begin_pass();   // pin fonts for this build pass
    pal_t p = settings_palette();
    const lv_font_t *f_title = font_cache_get(30, true);
    const lv_font_t *f_info  = font_cache_get(16, false);
    const lv_font_t *f_row   = font_cache_get(23, true);
    const lv_font_t *f_sub   = font_cache_get(16, false);
    const lv_font_t *f_exit  = font_cache_get(22, true);

    s_menu_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_menu_screen);
    lv_obj_set_size(s_menu_screen, 720, 720);
    lv_obj_set_pos(s_menu_screen, 0, 0);
    lv_obj_set_style_bg_color(s_menu_screen, p.bg, 0);
    lv_obj_set_style_bg_opa(s_menu_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int PAD = 40;

    // 1. Header — title left, info (s_menu_info_lbl) right.
    lv_obj_t *title = lv_label_create(s_menu_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, p.fg, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD, PAD);

    s_menu_info_lbl = lv_label_create(s_menu_screen);
    lv_label_set_long_mode(s_menu_info_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_menu_info_lbl, 300);
    lv_label_set_text(s_menu_info_lbl, s_info_text[0] ? s_info_text : "");
    lv_obj_set_style_text_font(s_menu_info_lbl, f_info, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_menu_info_lbl, p.muted, 0);
    lv_obj_set_style_text_align(s_menu_info_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_menu_info_lbl, LV_ALIGN_TOP_RIGHT, -PAD, PAD + 6);

    // 2. Card rows.
    struct { const char *icon; const char *label; const char *sub;
             ui_settings_action_t act; } rows[4] = {
        { LV_SYMBOL_WIFI,  "Network & Wi-Fi", "Scan and connect",      UI_SET_WIFI },
        { LV_SYMBOL_LIST,  "About",           "Device info",           UI_SET_ABOUT },
        { LV_SYMBOL_IMAGE, "Test print",      "Send a sample receipt", UI_SET_TEST_PRINT },
        { LV_SYMBOL_POWER, "Restart device",  "Reboots in ~30s",       UI_SET_REBOOT },
    };
    const int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));

    int row_x = PAD;
    int row_w = 720 - 2 * PAD;
    int row_h = 84;          // shrunk so 4 rows + Close still fit 720
    int row_gap = 12;
    int row_y0 = PAD + 56;   // below the header

    for (int i = 0; i < n_rows; i++) {
        lv_obj_t *btn = lv_button_create(s_menu_screen);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, row_w, row_h);
        lv_obj_set_pos(btn, row_x, row_y0 + i * (row_h + row_gap));
        lv_obj_set_style_radius(btn, 22, 0);
        lv_obj_set_style_bg_color(btn, p.card, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, p.fg, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_opa(btn, 28, 0);
        lv_obj_set_style_shadow_width(btn, 8, 0);
        lv_obj_set_style_shadow_opa(btn, 18, 0);
        lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
        lv_obj_set_style_shadow_color(btn, p.fg, 0);
        lv_obj_set_style_pad_left(btn, 22, 0);
        lv_obj_set_style_pad_right(btn, 24, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, menu_action_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)rows[i].act);

        // Left: 52x52 accent-soft icon chip.
        lv_obj_t *chip = make_icon_chip(btn, &p, 52, 16, rows[i].icon);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 0, 0);

        // Middle: label + sub (two lines).
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, rows[i].label);
        lv_obj_set_style_text_font(lbl, f_row, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, p.fg, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 52 + 20, -13);

        lv_obj_t *sub = lv_label_create(btn);
        lv_label_set_text(sub, rows[i].sub);
        lv_obj_set_style_text_font(sub, f_sub, LV_PART_MAIN);
        lv_obj_set_style_text_color(sub, p.muted, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 52 + 20, 13);

        // Right: chevron.
        lv_obj_t *chev = lv_label_create(btn);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, p.muted, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    // 3. Dark Exit button pinned at the bottom (full width, accent off — uses fg).
    lv_obj_t *exit_btn = lv_button_create(s_menu_screen);
    lv_obj_remove_style_all(exit_btn);
    lv_obj_set_size(exit_btn, row_w, 66);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_MID, 0, -PAD);
    lv_obj_set_style_radius(exit_btn, 20, 0);
    lv_obj_set_style_bg_color(exit_btn, p.fg, 0);
    lv_obj_set_style_bg_opa(exit_btn, LV_OPA_COVER, 0);
    lv_obj_clear_flag(exit_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(exit_btn, menu_action_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_SET_CLOSE);

    // Icon (default font) + "Close" (Open Sans) side by side, centered.
    lv_obj_t *exit_row = lv_obj_create(exit_btn);
    lv_obj_remove_style_all(exit_row);
    lv_obj_set_size(exit_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(exit_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(exit_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(exit_row, 12, 0);
    lv_obj_clear_flag(exit_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(exit_row);

    lv_obj_t *exit_ic = lv_label_create(exit_row);
    lv_label_set_text(exit_ic, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(exit_ic, p.bg, 0);

    lv_obj_t *exit_lbl = lv_label_create(exit_row);
    lv_label_set_text(exit_lbl, "Close");
    lv_obj_set_style_text_font(exit_lbl, f_exit, LV_PART_MAIN);
    lv_obj_set_style_text_color(exit_lbl, p.bg, 0);

    lvgl_port_unlock();
}

bool ui_settings_consume_action(ui_settings_action_t *act) {
    if (!s_action_set) return false;
    s_action_set = false;
    if (act) *act = s_action;
    return true;
}

// --------------------------------------------------------------------------
// Public API — About screen (read-only device info)
// --------------------------------------------------------------------------
// Renders `info_block` (one "label\tvalue" per line, '\n'-separated) as a card
// list: muted label left, fg value right. Online/Listening get a green dot,
// Offline a red dot. A back button (card-style LV_SYMBOL_LEFT chip) latches
// UI_SET_BACK via the shared action flag (reusing menu_action_cb). PASSIVE.

#define ABOUT_DOT_GREEN 0x2BB673
#define ABOUT_DOT_RED   0xE0564B

void ui_settings_show_about(lv_obj_t *parent, const char *info_block) {
    lvgl_port_lock(0);

    s_action_set = false;

    if (s_about_screen) { lv_obj_delete(s_about_screen); s_about_screen = NULL; }

    font_cache_begin_pass();
    pal_t p = settings_palette();
    const lv_font_t *f_title = font_cache_get(30, true);
    const lv_font_t *f_label = font_cache_get(18, false);
    const lv_font_t *f_value = font_cache_get(20, true);

    s_about_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_about_screen);
    lv_obj_set_size(s_about_screen, 720, 720);
    lv_obj_set_pos(s_about_screen, 0, 0);
    lv_obj_set_style_bg_color(s_about_screen, p.bg, 0);
    lv_obj_set_style_bg_opa(s_about_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_about_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int PAD = 40;

    // Header: back button (left) + "About" title.
    lv_obj_t *back = lv_button_create(s_about_screen);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 56, 56);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, PAD, PAD - 4);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_set_style_bg_color(back, p.card, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(back, p.fg, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_opa(back, 28, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back, menu_action_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_SET_BACK);
    lv_obj_t *back_ic = lv_label_create(back);
    lv_label_set_text(back_ic, LV_SYMBOL_LEFT);   // default font carries symbols
    lv_obj_set_style_text_color(back_ic, p.fg, 0);
    lv_obj_center(back_ic);

    lv_obj_t *title = lv_label_create(s_about_screen);
    lv_label_set_text(title, "About");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, p.fg, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD + 56 + 18, PAD + 6);

    // Card list of rows parsed from info_block. Vertical flex column, no scroll.
    lv_obj_t *list = lv_obj_create(s_about_screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 720 - 2 * PAD, LV_SIZE_CONTENT);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, PAD + 76);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 12, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    // Parse info_block: split on '\n', each line "label\tvalue".
    char buf[256];
    strncpy(buf, info_block ? info_block : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        if (!line[0]) continue;
        char *tab = strchr(line, '\t');
        const char *label = line;
        const char *value = "";
        if (tab) { *tab = '\0'; value = tab + 1; }

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 64);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_bg_color(row, p.card, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, p.fg, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_opa(row, 28, 0);
        lv_obj_set_style_pad_left(row, 22, 0);
        lv_obj_set_style_pad_right(row, 22, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, f_label, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, p.muted, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        // A status value gets a small colored dot to its left.
        bool good = (strcmp(value, "Online") == 0) || (strcmp(value, "Listening") == 0);
        bool bad  = (strcmp(value, "Offline") == 0);

        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_font(val, f_value, LV_PART_MAIN);
        lv_obj_set_style_text_color(val, p.fg, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

        if (good || bad) {
            lv_obj_t *dot = lv_obj_create(row);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 12, 12);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot,
                lv_color_hex(good ? ABOUT_DOT_GREEN : ABOUT_DOT_RED), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            // Sit just left of the value text (whose width depends on content).
            lv_obj_update_layout(val);
            lv_obj_align_to(dot, val, LV_ALIGN_OUT_LEFT_MID, -10, 0);
        }
    }

    lvgl_port_unlock();
}

// --------------------------------------------------------------------------
// Lifecycle reset
// --------------------------------------------------------------------------
// The PIN/menu containers are children of the settings screen. When that screen
// is deleted, LVGL auto-deletes those children — leaving our static pointers
// DANGLING. Calling this right after the screen is deleted clears them to NULL
// so the next ui_settings_show_pin/menu does not double-delete a freed object.
// It does NOT delete anything; the screen teardown already did.
void ui_settings_reset(void) {
    lvgl_port_lock(0);
    s_pin_screen    = NULL;
    s_pin_entry_lbl = NULL;
    s_pin_error_lbl = NULL;
    s_menu_screen   = NULL;
    s_menu_info_lbl = NULL;
    s_about_screen  = NULL;
    lvgl_port_unlock();
}
