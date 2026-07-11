#include "ui.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "font_cache.h"

// Bespoke, INTERACTIVE Wi-Fi setup screen (M6a-2; staged redesign 2026-07-11).
// PASSIVE by design: LVGL event callbacks ONLY set volatile flags/indices here
// — no network or heavy work. The orchestrator (wifi_setup_run) pushes data in
// (set_results/set_status/prompt_*/show_*) and polls the consume_* readers,
// mirroring the ui_consume_tap / ui_set_qr_url pattern in ui.c. Every public
// function takes the LVGL port lock internally, matching ui.c's discipline.
//
// TWO STAGES on ONE screen — panels toggled with LV_OBJ_FLAG_HIDDEN, every
// widget (keyboard included) created exactly once in ui_wifi_show():
//   list stage     — title + full-height network list (keyboard hidden)
//   password stage — back chip + network-name title + status line +
//                    textarea/Connect row + keyboard (unchanged 720x280)

#define DITTO_GREEN 0x10A765

// screen + list-stage widgets
static lv_obj_t *s_wscreen, *s_list_panel, *s_list_sub, *s_list;
// password-stage widgets
static lv_obj_t *s_pw_panel, *s_back, *s_pw_title, *s_pw_status,
                *s_input_row, *s_ta, *s_connect_lbl, *s_kb;

static volatile bool s_sel_pending;       // a list row was tapped
static volatile bool s_sel_manual;        // the tapped row was "Other network..."
static volatile bool s_connect_pending;   // Connect/Next was tapped
static volatile bool s_rescan_pending;    // Rescan was tapped
static volatile bool s_back_pending;      // Back chip was tapped
static volatile int  s_sel_idx;           // index of the tapped scan result (<0 = manual)

static char s_typed_ssid[33];             // SSID captured when entering password mode
static bool s_manual_mode;                // true = textarea holds an SSID, not a password

// --- event callbacks: flags only, no work ---------------------------------
static void list_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel_idx    = idx;
    s_sel_manual = (idx < 0);
    s_sel_pending = true;
}
static void connect_cb(lv_event_t *e) { (void)e; s_connect_pending = true; }
static void rescan_cb(lv_event_t *e)  { (void)e; s_rescan_pending = true; }
static void back_cb(lv_event_t *e)    { (void)e; s_back_pending = true; }

// --- internal helpers (caller must hold the LVGL port lock) ----------------

// A list row: translucent white card, symbol icon + brand-font text.
// Symbols stay on LVGL's default font (the brand TTF carries no symbol glyphs).
static void add_row(const char *symbol, const char *text, lv_event_cb_t cb, int idx) {
    lv_obj_t *btn = lv_button_create(s_list);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, lv_pct(100), 64);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED); // touch feedback
    lv_obj_set_style_pad_left(btn, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 20, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, symbol);                    // default font: symbol glyphs
    lv_obj_set_style_text_color(ic, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font_cache_get(22, false), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);   // ellipsize long SSIDs
    lv_obj_set_width(lbl, 680 - 2 * 20 - 44);         // row minus pads minus icon slot
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 44, 0);
}

// Stage switches. Drop stale one-shot intents from the outgoing stage so the
// orchestrator never acts on a tap that belonged to a widget no longer shown.
static void show_stage_list(void) {
    s_back_pending = false; s_connect_pending = false;
    lv_obj_add_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_list_panel, LV_OBJ_FLAG_HIDDEN);
}
static void show_stage_pw(bool with_input) {
    s_sel_pending = false; s_rescan_pending = false;
    s_connect_pending = false; s_back_pending = false;
    lv_obj_add_flag(s_list_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);
    if (with_input) {
        lv_obj_clear_flag(s_input_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_back, LV_OBJ_FLAG_HIDDEN);
    } else {                                          // "Connecting..." view
        lv_obj_add_flag(s_input_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- public API -------------------------------------------------------------

void ui_wifi_show(void) {
    lvgl_port_lock(0);
    // Re-entry (Settings -> Wi-Fi Setup): drop the previous screen so it
    // can't leak PSRAM or hold unpinned font-cache pointers.
    if (s_wscreen) { lv_obj_delete(s_wscreen); s_wscreen = NULL; }
    s_sel_pending = s_rescan_pending = s_connect_pending = s_back_pending = false;
    font_cache_begin_pass();                          // pin this screen's fonts
    const lv_font_t *f_title  = font_cache_get(28, true);
    const lv_font_t *f_status = font_cache_get(20, false);
    const lv_font_t *f_row    = font_cache_get(22, false);
    const lv_font_t *f_btn    = font_cache_get(22, true);
    (void)f_row;                                      // pinned for add_row()

    s_wscreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wscreen, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wscreen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_wscreen, LV_OBJ_FLAG_SCROLLABLE);

    // ---- list stage panel --------------------------------------------------
    s_list_panel = lv_obj_create(s_wscreen);
    lv_obj_remove_style_all(s_list_panel);
    lv_obj_set_size(s_list_panel, 720, 720);
    lv_obj_clear_flag(s_list_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_list_panel);
    lv_label_set_text(title, "Choose your Wi-Fi");
    lv_obj_set_style_text_font(title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    s_list_sub = lv_label_create(s_list_panel);       // status line (Scanning... etc.)
    lv_label_set_text(s_list_sub, "");
    lv_obj_set_style_text_font(s_list_sub, f_status, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_list_sub, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_list_sub, LV_OPA_80, LV_PART_MAIN);
    lv_obj_align(s_list_sub, LV_ALIGN_TOP_MID, 0, 70);

    // Plain scrollable flex column — full styling control (lv_list's theme
    // fights the brand look). 64px rows + 8px gaps, scrolls when overflowing.
    s_list = lv_obj_create(s_list_panel);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 680, 596);                // 104..700 of the 720 screen
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // ---- password stage panel (hidden until a network is picked) -----------
    s_pw_panel = lv_obj_create(s_wscreen);
    lv_obj_remove_style_all(s_pw_panel);
    lv_obj_set_size(s_pw_panel, 720, 720);
    lv_obj_clear_flag(s_pw_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);

    // Back chip, 56x56 (>=48px touch target), top-left.
    s_back = lv_button_create(s_pw_panel);
    lv_obj_remove_style_all(s_back);
    lv_obj_set_size(s_back, 56, 56);
    lv_obj_align(s_back, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_radius(s_back, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_back, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_40, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_clear_flag(s_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_ic = lv_label_create(s_back);
    lv_label_set_text(back_ic, LV_SYMBOL_LEFT);       // default font: symbol glyphs
    lv_obj_set_style_text_color(back_ic, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_ic);

    // Selected-network name — THE selection feedback.
    s_pw_title = lv_label_create(s_pw_panel);
    lv_label_set_text(s_pw_title, "");
    lv_obj_set_style_text_font(s_pw_title, f_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pw_title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(s_pw_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_pw_title, 500);                // clears the 56px back chip both sides
    lv_obj_set_style_text_align(s_pw_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_pw_title, LV_ALIGN_TOP_MID, 0, 34);

    s_pw_status = lv_label_create(s_pw_panel);
    lv_label_set_text(s_pw_status, "");
    lv_obj_set_style_text_font(s_pw_status, f_status, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pw_status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_pw_status, LV_OPA_80, LV_PART_MAIN);
    lv_obj_align(s_pw_status, LV_ALIGN_TOP_MID, 0, 100);

    // Input row: textarea (480) + Connect (180) side by side, 20px gap — the
    // two never overlap (the old screen's bug). Grouped so the "Connecting..."
    // view can hide them together.
    s_input_row = lv_obj_create(s_pw_panel);
    lv_obj_remove_style_all(s_input_row);
    lv_obj_set_size(s_input_row, 680, 60);
    lv_obj_align(s_input_row, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_clear_flag(s_input_row, LV_OBJ_FLAG_SCROLLABLE);

    s_ta = lv_textarea_create(s_input_row);
    lv_textarea_set_one_line(s_ta, true);
    lv_obj_set_size(s_ta, 480, 60);
    lv_obj_align(s_ta, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_ta, 12, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ta, f_status, LV_PART_MAIN);

    lv_obj_t *btn = lv_button_create(s_input_row);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, connect_cb, LV_EVENT_CLICKED, NULL);
    s_connect_lbl = lv_label_create(btn);
    lv_label_set_text(s_connect_lbl, "Connect");      // becomes "Next" in manual mode
    lv_obj_set_style_text_font(s_connect_lbl, f_btn, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_connect_lbl, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_center(s_connect_lbl);

    // Keyboard — UNCHANGED: same creation, size, position, default styling.
    s_kb = lv_keyboard_create(s_pw_panel);
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_obj_set_size(s_kb, 720, 280);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_screen_load(s_wscreen);
    lvgl_port_unlock();
}

void ui_wifi_show_list(void) {
    lvgl_port_lock(0);
    show_stage_list();
    lvgl_port_unlock();
}

void ui_wifi_show_connecting(const char *ssid) {
    lvgl_port_lock(0);
    lv_label_set_text(s_pw_title, ssid ? ssid : "");
    lv_label_set_text(s_pw_status, "Connecting...");
    lv_label_set_text(s_list_sub, "Connecting...");
    show_stage_pw(false);
    lvgl_port_unlock();
}

void ui_wifi_set_results(const net_ap_t *aps, int n) {
    lvgl_port_lock(0);
    lv_obj_clean(s_list);
    for (int i = 0; i < n; i++) add_row(LV_SYMBOL_WIFI, aps[i].ssid, list_cb, i);
    add_row(LV_SYMBOL_PLUS, "Other network...", list_cb, -1);
    add_row(LV_SYMBOL_REFRESH, "Rescan", rescan_cb, -1);
    lvgl_port_unlock();
}

void ui_wifi_set_status(const char *t) {
    lvgl_port_lock(0);
    lv_label_set_text(s_list_sub, t ? t : "");
    lv_label_set_text(s_pw_status, t ? t : "");
    lvgl_port_unlock();
}

void ui_wifi_prompt_ssid(void) {
    lvgl_port_lock(0);
    s_manual_mode = true;
    lv_label_set_text(s_pw_title, "Other network");
    lv_label_set_text(s_pw_status, "Enter the network name");
    lv_label_set_text(s_connect_lbl, "Next");
    lv_textarea_set_password_mode(s_ta, false);
    lv_textarea_set_text(s_ta, "");
    lv_textarea_set_placeholder_text(s_ta, "Network name");
    show_stage_pw(true);
    lvgl_port_unlock();
}

void ui_wifi_prompt_password(const char *ssid) {
    lvgl_port_lock(0);
    s_manual_mode = false;
    strncpy(s_typed_ssid, ssid ? ssid : "", sizeof(s_typed_ssid) - 1);
    s_typed_ssid[sizeof(s_typed_ssid) - 1] = '\0';
    lv_label_set_text(s_pw_title, s_typed_ssid);
    lv_label_set_text(s_pw_status, "Enter the Wi-Fi password");
    lv_label_set_text(s_connect_lbl, "Connect");
    lv_textarea_set_password_mode(s_ta, true);
    lv_textarea_set_text(s_ta, "");
    lv_textarea_set_placeholder_text(s_ta, "Password");
    show_stage_pw(true);
    lvgl_port_unlock();
}

bool ui_wifi_consume_selection(int *idx, bool *manual) {
    if (!s_sel_pending) return false;
    s_sel_pending = false;
    if (idx)    *idx = s_sel_idx;
    if (manual) *manual = s_sel_manual;
    return true;
}

bool ui_wifi_consume_rescan(void) {
    bool r = s_rescan_pending;
    s_rescan_pending = false;
    return r;
}

bool ui_wifi_consume_back(void) {
    lvgl_port_lock(0);
    bool r = s_back_pending;
    s_back_pending = false;
    lvgl_port_unlock();
    return r;
}

bool ui_wifi_consume_connect(char *ssid, int sc, char *pass, int pc) {
    if (!s_connect_pending) return false;
    s_connect_pending = false;
    // Reads the textarea, so take the lock (matches ui.c's consume locking).
    lvgl_port_lock(0);
    const char *txt = lv_textarea_get_text(s_ta);
    if (s_manual_mode) {
        strncpy(ssid, txt, sc - 1); ssid[sc - 1] = '\0';
        pass[0] = '\0';
    } else {
        strncpy(ssid, s_typed_ssid, sc - 1); ssid[sc - 1] = '\0';
        strncpy(pass, txt, pc - 1);          pass[pc - 1] = '\0';
    }
    lvgl_port_unlock();
    return true;
}
