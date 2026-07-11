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
// VISUAL: restyled to match the Ditto staff mockups (PinScreen / MenuScreen).
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
static lv_obj_t *s_about_screen;
static lv_obj_t *s_device_screen;

// --- Device-settings sub-screen volatile latches (PASSIVE callbacks set these) -
static volatile bool s_dev_use_set;       // "Use device settings" switch flipped
static volatile bool s_dev_use;           // new switch state
static volatile bool s_dev_br_set;        // slider moved (live)
static volatile int  s_dev_br;            // live brightness 10..100
static volatile bool s_dev_br_commit;     // slider released (persist)
static volatile int  s_dev_br_commit_val; // committed brightness 10..100
static volatile bool s_dev_sl_set;        // a sleep chip was tapped
static volatile int  s_dev_sl;            // chosen encoded value 0/60/120/300/600

static volatile ui_settings_action_t s_action;
static volatile bool                 s_action_set;

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
// Public API — Menu screen
// --------------------------------------------------------------------------

void ui_settings_show_menu(lv_obj_t *parent) {
    lvgl_port_lock(0);

    s_action_set = false;

    // Delete any previous instance (menu + any sub-screen overlaying it).
    if (s_menu_screen) { lv_obj_delete(s_menu_screen); s_menu_screen = NULL; }
    if (s_about_screen) { lv_obj_delete(s_about_screen); s_about_screen = NULL; }
    if (s_device_screen) { lv_obj_delete(s_device_screen); s_device_screen = NULL; }

    font_cache_begin_pass();   // pin fonts for this build pass
    pal_t p = settings_palette();
    const lv_font_t *f_title = font_cache_get(30, true);
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

    // 1. Header — title only. (Device facts live on the About sub-screen.)
    lv_obj_t *title = lv_label_create(s_menu_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, p.fg, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD, PAD);

    // 2. Card rows.
    struct { const char *icon; const char *label; const char *sub;
             ui_settings_action_t act; } rows[4] = {
        { LV_SYMBOL_WIFI,     "Network & Wi-Fi", "Scan and connect",      UI_SET_WIFI },
        { LV_SYMBOL_SETTINGS, "Device settings", "Brightness, sleep",     UI_SET_DEVICE },
        { LV_SYMBOL_LIST,     "About",           "Device info",           UI_SET_ABOUT },
        { LV_SYMBOL_POWER,    "Restart device",  "Reboots in ~30s",       UI_SET_REBOOT },
    };
    const int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));

    int row_x = PAD;
    int row_w = 720 - 2 * PAD;
    int row_h = 72;          // shrunk so 5 rows + Close still fit 720
    int row_gap = 10;
    int row_y0 = PAD + 52;   // below the header

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
// Public API — Device settings screen (interactive brightness + sleep)
// --------------------------------------------------------------------------
// A bespoke INTERACTIVE sub-screen. PASSIVE: every event callback only sets a
// volatile flag/value. The orchestrator reads them via the consume_* readers,
// applies brightness live (VALUE_CHANGED) but persists only on RELEASE (commit)
// to avoid thrashing NVS on every slider tick.

// Sleep options offered on screen, encoded as seconds (0 = off).
static const int  k_sleep_encoded[5] = { 0, 60, 120, 300, 600 };
static const char *k_sleep_label[5]  = { "Off", "1 min", "2 min", "5 min", "10 min" };

// --- Device-settings event callbacks: flags/values only, no work -----------
static void dev_brightness_changed_cb(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    s_dev_br     = (int)lv_slider_get_value(sl);   // 10..100
    s_dev_br_set = true;
}

static void dev_brightness_released_cb(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    s_dev_br_commit_val = (int)lv_slider_get_value(sl);
    s_dev_br_commit     = true;
}

static void dev_sleep_cb(lv_event_t *e) {
    s_dev_sl     = (int)(intptr_t)lv_event_get_user_data(e);  // encoded seconds
    s_dev_sl_set = true;
}

static void dev_use_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    s_dev_use     = lv_obj_has_state(sw, LV_STATE_CHECKED);
    s_dev_use_set = true;
}

void ui_settings_show_device(lv_obj_t *parent, bool use_device,
                             int brightness, int sleep_encoded) {
    lvgl_port_lock(0);

    s_action_set = false;
    // Drain any stale latches from a prior build so the new screen starts clean.
    s_dev_use_set = s_dev_br_set = s_dev_br_commit = s_dev_sl_set = false;

    if (s_device_screen) { lv_obj_delete(s_device_screen); s_device_screen = NULL; }

    if (brightness < 10)  brightness = 10;
    if (brightness > 100) brightness = 100;

    font_cache_begin_pass();
    pal_t p = settings_palette();
    const lv_font_t *f_title = font_cache_get(30, true);
    const lv_font_t *f_card  = font_cache_get(22, true);
    const lv_font_t *f_chip  = font_cache_get(18, true);
    const lv_font_t *f_ghost = font_cache_get(16, true);

    s_device_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_device_screen);
    lv_obj_set_size(s_device_screen, 720, 720);
    lv_obj_set_pos(s_device_screen, 0, 0);
    lv_obj_set_style_bg_color(s_device_screen, p.bg, 0);
    lv_obj_set_style_bg_opa(s_device_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_device_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int PAD = 40;

    // Header: back button (left) + "Device settings" title.
    lv_obj_t *back = lv_button_create(s_device_screen);
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

    lv_obj_t *title = lv_label_create(s_device_screen);
    lv_label_set_text(title, "Device settings");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, p.fg, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD + 56 + 18, PAD + 6);

    int card_x = PAD;
    int card_w = 720 - 2 * PAD;

    // --- "Use device settings" source switch (top) -------------------------
    // When OFF, the brightness/sleep controls follow the admin policy and are
    // shown disabled + dimmed; when ON they're editable.
    lv_obj_t *src_card = lv_obj_create(s_device_screen);
    lv_obj_remove_style_all(src_card);
    lv_obj_set_size(src_card, card_w, 88);
    lv_obj_set_pos(src_card, card_x, PAD + 76);
    lv_obj_set_style_radius(src_card, 22, 0);
    lv_obj_set_style_bg_color(src_card, p.card, 0);
    lv_obj_set_style_bg_opa(src_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(src_card, p.fg, 0);
    lv_obj_set_style_border_width(src_card, 1, 0);
    lv_obj_set_style_border_opa(src_card, 28, 0);
    lv_obj_set_style_pad_all(src_card, 24, 0);
    lv_obj_clear_flag(src_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *src_lbl = lv_label_create(src_card);
    lv_label_set_text(src_lbl, "Use device settings");
    lv_obj_set_style_text_font(src_lbl, f_card, LV_PART_MAIN);
    lv_obj_set_style_text_color(src_lbl, p.fg, 0);
    lv_obj_align(src_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *src_sub = lv_label_create(src_card);
    lv_label_set_text(src_sub, use_device ? "Editing on this device" : "Following admin");
    lv_obj_set_style_text_font(src_sub, f_ghost, LV_PART_MAIN);
    lv_obj_set_style_text_color(src_sub, p.muted, 0);
    lv_obj_align(src_sub, LV_ALIGN_LEFT_MID, 0, 22);

    lv_obj_t *src_sw = lv_switch_create(src_card);
    lv_obj_align(src_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(src_sw, lv_color_mix(p.fg, p.bg, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(src_sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(src_sw, p.accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(src_sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (use_device) lv_obj_add_state(src_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(src_sw, dev_use_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Controls are dimmed + non-interactive while following admin.
    const lv_opa_t ctrl_opa = use_device ? LV_OPA_COVER : LV_OPA_50;

    // --- Brightness card ---------------------------------------------------
    lv_obj_t *br_card = lv_obj_create(s_device_screen);
    lv_obj_remove_style_all(br_card);
    lv_obj_set_size(br_card, card_w, 170);
    lv_obj_set_pos(br_card, card_x, PAD + 76 + 88 + 16);
    lv_obj_set_style_radius(br_card, 22, 0);
    lv_obj_set_style_bg_color(br_card, p.card, 0);
    lv_obj_set_style_bg_opa(br_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(br_card, p.fg, 0);
    lv_obj_set_style_border_width(br_card, 1, 0);
    lv_obj_set_style_border_opa(br_card, 28, 0);
    lv_obj_set_style_pad_all(br_card, 24, 0);
    lv_obj_set_style_opa(br_card, ctrl_opa, 0);
    lv_obj_clear_flag(br_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *br_lbl = lv_label_create(br_card);
    lv_label_set_text(br_lbl, "Brightness");
    lv_obj_set_style_text_font(br_lbl, f_card, LV_PART_MAIN);
    lv_obj_set_style_text_color(br_lbl, p.fg, 0);
    lv_obj_align(br_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *slider = lv_slider_create(br_card);
    lv_obj_set_width(slider, card_w - 2 * 24);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_mix(p.fg, p.bg, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, p.accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, p.accent, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, dev_brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, dev_brightness_released_cb,
                        LV_EVENT_RELEASED, NULL);
    // When following admin, disable + make the slider non-interactive.
    if (!use_device) {
        lv_obj_add_state(slider, LV_STATE_DISABLED);
        lv_obj_clear_flag(slider, LV_OBJ_FLAG_CLICKABLE);
    }

    // --- Sleep card --------------------------------------------------------
    lv_obj_t *sl_card = lv_obj_create(s_device_screen);
    lv_obj_remove_style_all(sl_card);
    lv_obj_set_size(sl_card, card_w, 200);
    lv_obj_set_pos(sl_card, card_x, PAD + 76 + 88 + 16 + 170 + 16);
    lv_obj_set_style_radius(sl_card, 22, 0);
    lv_obj_set_style_bg_color(sl_card, p.card, 0);
    lv_obj_set_style_bg_opa(sl_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sl_card, p.fg, 0);
    lv_obj_set_style_border_width(sl_card, 1, 0);
    lv_obj_set_style_border_opa(sl_card, 28, 0);
    lv_obj_set_style_pad_all(sl_card, 24, 0);
    lv_obj_set_style_opa(sl_card, ctrl_opa, 0);
    lv_obj_clear_flag(sl_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sl_lbl = lv_label_create(sl_card);
    lv_label_set_text(sl_lbl, "Sleep after");
    lv_obj_set_style_text_font(sl_lbl, f_card, LV_PART_MAIN);
    lv_obj_set_style_text_color(sl_lbl, p.fg, 0);
    lv_obj_align(sl_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    // 5 selectable chips in a flex row; highlight the one matching sleep_encoded.
    lv_obj_t *chips = lv_obj_create(sl_card);
    lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips, card_w - 2 * 24, 64);
    lv_obj_align(chips, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chips, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        bool sel = (k_sleep_encoded[i] == sleep_encoded);
        lv_obj_t *chip = lv_button_create(chips);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 110, 60);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_bg_color(chip, sel ? p.accent : p.bg, 0);
        lv_obj_set_style_bg_opa(chip, sel ? LV_OPA_COVER : 40, 0);
        lv_obj_set_style_border_color(chip, sel ? p.accent : p.fg, 0);
        lv_obj_set_style_border_width(chip, sel ? 0 : 1, 0);
        lv_obj_set_style_border_opa(chip, sel ? 0 : 28, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(chip, dev_sleep_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)k_sleep_encoded[i]);
        if (!use_device) {
            lv_obj_add_state(chip, LV_STATE_DISABLED);
            lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *cl = lv_label_create(chip);
        lv_label_set_text(cl, k_sleep_label[i]);
        lv_obj_set_style_text_font(cl, f_chip, LV_PART_MAIN);
        lv_obj_set_style_text_color(cl, sel ? p.accent_fg : p.fg, 0);
        lv_obj_center(cl);
    }

    lvgl_port_unlock();
}

bool ui_settings_consume_use_device(bool *on) {
    if (!s_dev_use_set) return false;
    s_dev_use_set = false;
    if (on) *on = s_dev_use;
    return true;
}

bool ui_settings_consume_brightness(int *val) {
    if (!s_dev_br_set) return false;
    s_dev_br_set = false;
    if (val) *val = s_dev_br;
    return true;
}

bool ui_settings_consume_brightness_commit(int *val) {
    if (!s_dev_br_commit) return false;
    s_dev_br_commit = false;
    if (val) *val = s_dev_br_commit_val;
    return true;
}

bool ui_settings_consume_sleep(int *encoded) {
    if (!s_dev_sl_set) return false;
    s_dev_sl_set = false;
    if (encoded) *encoded = s_dev_sl;
    return true;
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
    s_about_screen  = NULL;
    s_device_screen = NULL;
    lvgl_port_unlock();
}
