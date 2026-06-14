#include "ui.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"   // lvgl_port_lock/unlock — the standard ESP-IDF LVGL port.

// Brand palette (mirrors ditto-admin defaults: brandColor #10A765).
#define DITTO_GREEN   0x10A765
#define DITTO_ONLINE  0x37C871
#define DITTO_GREY    0x888888

static lv_obj_t *s_idle_screen;
static lv_obj_t *s_processing_screen;
static lv_obj_t *s_qr_screen;
static lv_obj_t *s_qr;
static lv_obj_t *s_status_dot;
static volatile bool s_tap_requested;

static void on_screen_click(lv_event_t *e) { (void)e; s_tap_requested = true; }

static lv_obj_t *make_idle_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ditto");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Ready");
    lv_obj_set_style_text_color(sub, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 44);

    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 20, 20);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(DITTO_GREY), LV_PART_MAIN);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -16, 16);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click, LV_EVENT_CLICKED, NULL);
    return scr;
}

static lv_obj_t *make_processing_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Processing...");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_center(label);
    return scr;
}

static lv_obj_t *make_qr_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Scan for your receipt");
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 56);

    s_qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(s_qr, 420);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_center(s_qr);

    // Tapping the QR screen re-triggers a test ingest (M3 convenience).
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click, LV_EVENT_CLICKED, NULL);
    return scr;
}

void ui_init(void)
{
    lvgl_port_lock(0);
    s_idle_screen = make_idle_screen();
    s_processing_screen = make_processing_screen();
    s_qr_screen = make_qr_screen();
    lvgl_port_unlock();
}

void ui_show(ui_screen_t screen)
{
    lv_obj_t *target = NULL;
    if (screen == UI_SCREEN_IDLE) target = s_idle_screen;
    else if (screen == UI_SCREEN_PROCESSING) target = s_processing_screen;
    if (!target) return;
    lvgl_port_lock(0);
    lv_screen_load(target);
    lvgl_port_unlock();
}

void ui_set_online(bool online)
{
    if (s_status_dot) {
        lvgl_port_lock(0);
        lv_obj_set_style_bg_color(s_status_dot,
            lv_color_hex(online ? DITTO_ONLINE : DITTO_GREY), LV_PART_MAIN);
        lvgl_port_unlock();
    }
}

void ui_show_qr(const char *url)
{
    if (!s_qr_screen || !s_qr || !url) return;
    lvgl_port_lock(0);
    lv_qrcode_update(s_qr, url, strlen(url));
    lv_screen_load(s_qr_screen);
    lvgl_port_unlock();
}

bool ui_consume_tap(void)
{
    bool t = s_tap_requested;
    s_tap_requested = false;
    return t;
}
