#include "ui.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"

// Bespoke, INTERACTIVE Wi-Fi setup screen (M6a-2). PASSIVE by design: LVGL event
// callbacks ONLY set volatile flags/indices here — no network or heavy work. An
// orchestrator (Task 5) pushes data in (set_results/set_status/prompt_*) and polls
// the consume_* readers for user intent, mirroring the ui_consume_tap /
// ui_set_qr_url pattern in ui.c. Every public function takes the LVGL port lock
// internally, matching ui.c's locking discipline.

#define DITTO_GREEN 0x10A765

static lv_obj_t  *s_wscreen, *s_status, *s_list, *s_ta, *s_kb;

static volatile bool s_sel_pending;       // a list row was tapped
static volatile bool s_sel_manual;        // the tapped row was "Other network..."
static volatile bool s_connect_pending;   // Connect was tapped
static volatile bool s_rescan_pending;    // Rescan was tapped
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

void ui_wifi_show(void) {
    lvgl_port_lock(0);
    s_wscreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wscreen, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wscreen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_wscreen, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(s_wscreen);
    lv_label_set_text(s_status, "Wi-Fi setup");
    lv_obj_set_style_text_color(s_status, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 12);

    s_list = lv_list_create(s_wscreen);
    lv_obj_set_size(s_list, 680, 300);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 48);

    s_ta = lv_textarea_create(s_wscreen);
    lv_textarea_set_one_line(s_ta, true);
    lv_obj_set_width(s_ta, 680);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 356);

    s_kb = lv_keyboard_create(s_wscreen);
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_obj_set_size(s_kb, 720, 280);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *btn = lv_button_create(s_wscreen);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -16, 352);
    lv_obj_add_event_cb(btn, connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Connect");

    lv_screen_load(s_wscreen);
    lvgl_port_unlock();
}

void ui_wifi_set_results(const net_ap_t *aps, int n) {
    lvgl_port_lock(0);
    lv_obj_clean(s_list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *it = lv_list_add_button(s_list, LV_SYMBOL_WIFI, aps[i].ssid);
        lv_obj_add_event_cb(it, list_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *other = lv_list_add_button(s_list, LV_SYMBOL_PLUS, "Other network...");
    lv_obj_add_event_cb(other, list_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *re = lv_list_add_button(s_list, LV_SYMBOL_REFRESH, "Rescan");
    lv_obj_add_event_cb(re, rescan_cb, LV_EVENT_CLICKED, NULL);
    lvgl_port_unlock();
}

void ui_wifi_set_status(const char *t) {
    lvgl_port_lock(0);
    lv_label_set_text(s_status, t ? t : "");
    lvgl_port_unlock();
}

void ui_wifi_prompt_ssid(void) {
    lvgl_port_lock(0);
    s_manual_mode = true;
    lv_textarea_set_password_mode(s_ta, false);
    lv_textarea_set_text(s_ta, "");
    lv_textarea_set_placeholder_text(s_ta, "Network name");
    lvgl_port_unlock();
}

void ui_wifi_prompt_password(const char *ssid) {
    lvgl_port_lock(0);
    s_manual_mode = false;
    strncpy(s_typed_ssid, ssid ? ssid : "", sizeof(s_typed_ssid) - 1);
    s_typed_ssid[sizeof(s_typed_ssid) - 1] = '\0';
    lv_textarea_set_password_mode(s_ta, true);
    lv_textarea_set_text(s_ta, "");
    lv_textarea_set_placeholder_text(s_ta, "Password");
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
