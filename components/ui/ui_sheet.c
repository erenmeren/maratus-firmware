#include "ui.h"
#include <stdint.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "gesture.h"
#include "device_config.h"

// Interactive Settings bottom-sheet. Lives on lv_layer_top(), above whatever
// branding screen is loaded. Drag/animation run on the LVGL thread; poll_task
// only learns "fully open" via ui_consume_sheet_open().

#define SCREEN_H            720
#define SHEET_DRAG_SLOP_PX  12      // movement before a press becomes a drag
#define SHEET_OPEN_PCT      40      // % revealed that auto-opens
#define SHEET_FLICK_PX_S    1200    // upward flick (px/s) that opens regardless
#define SHEET_ANIM_MS       220     // snap glide duration

static lv_obj_t      *s_sheet;          // full-screen container, NULL when absent
static bool           s_dragging;       // crossed the slop this gesture
static int            s_press_y;        // gesture start y
static int            s_prev_y;         // last sampled y (for velocity)
static uint32_t       s_prev_tick;      // last sample tick (ms)
static int            s_vel_up;         // last computed upward velocity px/s
static volatile bool  s_open;           // set when open animation completes

static int sheet_brand_bg(void) {
    const device_config_t *c = ui_current_cfg();
    return (c && c->valid) ? (int)c->brand_bg : (int)0x101418;
}

static void build_sheet(void) {
    if (s_sheet) return;
    s_sheet = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_sheet);
    lv_obj_set_size(s_sheet, 720, SCREEN_H);
    lv_obj_set_y(s_sheet, SCREEN_H);                 // start fully below the screen
    lv_obj_set_style_bg_color(s_sheet, lv_color_hex(sheet_brand_bg()), 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_sheet, 28, 0);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

    // Grab-handle pill
    lv_obj_t *grip = lv_obj_create(s_sheet);
    lv_obj_remove_style_all(grip);
    lv_obj_set_size(grip, 120, 8);
    lv_obj_set_style_radius(grip, 4, 0);
    lv_obj_set_style_bg_color(grip, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_40, 0);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, 18);

    // Title
    lv_obj_t *title = lv_label_create(s_sheet);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);
}

static void destroy_sheet(void) {
    if (s_sheet) { lv_obj_delete(s_sheet); s_sheet = NULL; }
    s_dragging = false;
}

static void open_anim_done(lv_anim_t *a) { (void)a; s_open = true; }
static void close_anim_done(lv_anim_t *a) { (void)a; destroy_sheet(); }

static void animate_to(int target_y, lv_anim_completed_cb_t done) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_sheet);
    lv_anim_set_values(&a, lv_obj_get_y(s_sheet), target_y);
    lv_anim_set_duration(&a, SHEET_ANIM_MS);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

void ui_sheet_on_press(int x, int y) {
    (void)x;
    s_press_y = y; s_prev_y = y; s_prev_tick = lv_tick_get();
    s_dragging = false; s_vel_up = 0;
}

void ui_sheet_on_pressing(int x, int y) {
    (void)x;
    int up = s_press_y - y;                       // positive = moved up
    if (!s_dragging) {
        if (up < SHEET_DRAG_SLOP_PX) return;      // still a potential tap
        s_dragging = true;
        build_sheet();
    }
    int reveal = up; if (reveal < 0) reveal = 0; if (reveal > SCREEN_H) reveal = SCREEN_H;
    lv_obj_set_y(s_sheet, SCREEN_H - reveal);

    uint32_t now = lv_tick_get();
    uint32_t dt = now - s_prev_tick;
    if (dt >= 16) {                               // ~1 frame; compute px/s
        s_vel_up = (int)((long)(s_prev_y - y) * 1000 / (long)dt);
        s_prev_y = y; s_prev_tick = now;
    }
}

void ui_sheet_on_release(void) {
    if (!s_dragging) return;
    s_dragging = false;
    ui_suppress_next_click();                     // the trailing CLICKED is part of this drag
    int reveal = SCREEN_H - lv_obj_get_y(s_sheet);
    int vel = s_vel_up > 0 ? s_vel_up : 0;
    if (gesture_snap(reveal, SCREEN_H, vel, SHEET_OPEN_PCT, SHEET_FLICK_PX_S) == SNAP_OPEN) {
        animate_to(0, open_anim_done);
    } else {
        animate_to(SCREEN_H, close_anim_done);
    }
}

bool ui_consume_sheet_open(void) {
    lvgl_port_lock(0);
    bool v = s_open; s_open = false;
    lvgl_port_unlock();
    return v;
}

void ui_sheet_close(void) {
    lvgl_port_lock(0);
    if (s_sheet) animate_to(SCREEN_H, close_anim_done);
    lvgl_port_unlock();
}

lv_obj_t *ui_sheet_content(void) {
    lvgl_port_lock(0);
    lv_obj_t *c = s_sheet;
    lvgl_port_unlock();
    return c;
}

void ui_sheet_set_hidden(bool hidden) {
    lvgl_port_lock(0);
    if (s_sheet) {
        if (hidden) lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}
