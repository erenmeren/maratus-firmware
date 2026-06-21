#include "ui.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"

// On-device Settings screens (M7). PASSIVE by design: LVGL event callbacks
// ONLY set volatile flags here — no network, no esp_restart, no cloud calls.
// An orchestrator (Task 7) reads user intent via the consume_* functions and
// pushes data in via the set_* mutators, mirroring the ui_wifi.c pattern.
// Every public function takes the LVGL port lock internally, matching ui.c's
// locking discipline.

// --------------------------------------------------------------------------
// PIN keypad screen
// --------------------------------------------------------------------------

static lv_obj_t *s_pin_screen;
static lv_obj_t *s_pin_entry_lbl;   // masked display of typed digits
static lv_obj_t *s_pin_error_lbl;   // "Incorrect PIN" — hidden by default

static char s_pin_buf[16];
static volatile bool s_pin_submitted;

// Rebuild the masked entry label from s_pin_buf.
// Must be called while holding the LVGL lock.
static void pin_refresh_display(void) {
    if (!s_pin_entry_lbl) return;
    int len = (int)strlen(s_pin_buf);
    char mask[17];
    for (int i = 0; i < len && i < (int)(sizeof(mask) - 1); i++) mask[i] = '*';
    mask[len < (int)(sizeof(mask) - 1) ? len : (int)(sizeof(mask) - 1)] = '\0';
    lv_label_set_text(s_pin_entry_lbl, len > 0 ? mask : " ");
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
// Helper: make a labelled button
// --------------------------------------------------------------------------
static lv_obj_t *make_button(lv_obj_t *parent, const char *label,
                              lv_event_cb_t cb, void *user_data) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    return btn;
}

// --------------------------------------------------------------------------
// Public API — PIN screen
// --------------------------------------------------------------------------

void ui_settings_show_pin(void) {
    lvgl_port_lock(0);

    // Reset state
    s_pin_buf[0]   = '\0';
    s_pin_submitted = false;

    // Delete any previous instance
    if (s_pin_screen) { lv_obj_delete(s_pin_screen); s_pin_screen = NULL; }

    s_pin_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_pin_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(s_pin_screen);
    lv_label_set_text(title, "Enter PIN");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Masked entry display
    s_pin_entry_lbl = lv_label_create(s_pin_screen);
    lv_label_set_text(s_pin_entry_lbl, " ");
    lv_obj_align(s_pin_entry_lbl, LV_ALIGN_TOP_MID, 0, 70);

    // Error label (hidden by default)
    s_pin_error_lbl = lv_label_create(s_pin_screen);
    lv_label_set_text(s_pin_error_lbl, "Incorrect PIN");
    lv_obj_align(s_pin_error_lbl, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_flag(s_pin_error_lbl, LV_OBJ_FLAG_HIDDEN);

    // 3×4 numeric keypad layout:
    //   1 2 3
    //   4 5 6
    //   7 8 9
    //   ← 0 OK
    static const char *const KEY_LABELS[12] = {
        "1","2","3",
        "4","5","6",
        "7","8","9",
        LV_SYMBOL_BACKSPACE, "0", "OK"
    };

    int btn_w = 160, btn_h = 80, gap_x = 20, gap_y = 16;
    int grid_w = 3 * btn_w + 2 * gap_x;
    int start_x = (720 - grid_w) / 2;
    int start_y = 160;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int x = start_x + col * (btn_w + gap_x);
            int y = start_y + row * (btn_h + gap_y);

            lv_obj_t *btn = lv_button_create(s_pin_screen);
            lv_obj_set_size(btn, btn_w, btn_h);
            lv_obj_set_pos(btn, x, y);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, KEY_LABELS[idx]);
            lv_obj_center(lbl);

            if (row == 3 && col == 0) {
                // Backspace
                lv_obj_add_event_cb(btn, pin_backspace_cb, LV_EVENT_CLICKED, NULL);
            } else if (row == 3 && col == 2) {
                // OK / submit
                lv_obj_add_event_cb(btn, pin_ok_cb, LV_EVENT_CLICKED, NULL);
            } else {
                // Digit: 1-9 on rows 0-2, 0 at row 3 col 1
                int digit;
                if (row < 3) {
                    digit = row * 3 + col + 1;  // 1..9
                } else {
                    digit = 0;
                }
                lv_obj_add_event_cb(btn, pin_digit_cb, LV_EVENT_CLICKED,
                                    (void *)(intptr_t)digit);
            }
        }
    }

    // Cancel button (below keypad)
    lv_obj_t *cancel = make_button(s_pin_screen, "Cancel", pin_cancel_cb, NULL);
    lv_obj_set_size(cancel, 160, 60);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_screen_load(s_pin_screen);
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
    if (s_pin_entry_lbl) lv_label_set_text(s_pin_entry_lbl, " ");
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

void ui_settings_show_menu(void) {
    lvgl_port_lock(0);

    s_action_set = false;

    // Delete any previous instance
    if (s_menu_screen) { lv_obj_delete(s_menu_screen); s_menu_screen = NULL; }

    s_menu_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(s_menu_screen);
    lv_label_set_text(title, "Device Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Info label (multi-line device info block)
    s_menu_info_lbl = lv_label_create(s_menu_screen);
    lv_label_set_long_mode(s_menu_info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_menu_info_lbl, 680);
    lv_label_set_text(s_menu_info_lbl, s_info_text[0] ? s_info_text : "");
    lv_obj_align(s_menu_info_lbl, LV_ALIGN_TOP_MID, 0, 70);

    // Four action buttons, centered, evenly spaced vertically
    static const char *const BTN_LABELS[4] = {
        "Wi-Fi Setup", "Test Print", "Reboot", "Close"
    };
    static const ui_settings_action_t BTN_ACTIONS[4] = {
        UI_SET_WIFI, UI_SET_TEST_PRINT, UI_SET_REBOOT, UI_SET_CLOSE
    };

    int btn_w = 400, btn_h = 72, gap_y = 20;
    int total_h = 4 * btn_h + 3 * gap_y;
    int start_y = (720 - total_h) / 2 + 60;  // shift down a bit for the info block

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(s_menu_screen);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, start_y + i * (btn_h + gap_y));
        lv_obj_add_event_cb(btn, menu_action_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)BTN_ACTIONS[i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, BTN_LABELS[i]);
        lv_obj_center(lbl);
    }

    lv_screen_load(s_menu_screen);
    lvgl_port_unlock();
}

bool ui_settings_consume_action(ui_settings_action_t *act) {
    if (!s_action_set) return false;
    s_action_set = false;
    if (act) *act = s_action;
    return true;
}
