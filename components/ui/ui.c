#include "ui.h"
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "device_config.h"
#include "render_geom.h"
#include "font_cache.h"
#include "assets.h"

#define DITTO_GREEN   0x10A765
#define DITTO_ONLINE  0x37C871
#define DITTO_GREY    0x888888

static const device_config_t *s_cfg;
static lv_obj_t  *s_active;            // currently displayed screen
static lv_obj_t  *s_status_dot;        // chrome dot on the active screen
static char       s_qr_url[256];
static bool       s_online;
static volatile bool s_tap_requested;

static void on_screen_click(lv_event_t *e) { (void)e; s_tap_requested = true; }

static lv_text_align_t lv_align_of(cfg_align_t a) {
    if (a == CFG_ALIGN_CENTER) return LV_TEXT_ALIGN_CENTER;
    if (a == CFG_ALIGN_RIGHT)  return LV_TEXT_ALIGN_RIGHT;
    return LV_TEXT_ALIGN_LEFT;
}

static void add_status_dot(lv_obj_t *scr) {
    s_status_dot = lv_obj_create(scr);
    lv_obj_set_size(s_status_dot, 20, 20);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_status_dot,
        lv_color_hex(s_online ? DITTO_ONLINE : DITTO_GREY), LV_PART_MAIN);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -16, 16);
}

static void render_text(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, b.w > 0 ? b.w : LV_SIZE_CONTENT);
    lv_label_set_text(lbl, o->text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font_cache_get(o->font_size), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, lv_align_of(o->align), LV_PART_MAIN);
    lv_obj_set_pos(lbl, b.x, b.y);
}

static void render_qr(lv_obj_t *scr, const cfg_object_t *o) {
    px_box_t b = geom_box(o);
    int side = b.w < b.h ? b.w : b.h;
    if (side < 32) side = 32;
    lv_obj_t *qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(qr, side);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    const char *url = s_qr_url[0] ? s_qr_url : "https://ditto.app";
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_set_pos(qr, b.x, b.y);
}

// Placeholder box for object types implemented in M5b/M5c (logo/icon/clock/wifi/
// spinner/countdown/pairingCode/steps). A faint outline so layout is visible
// without faking content.
static void render_placeholder(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    lv_obj_t *ph = lv_obj_create(scr);
    lv_obj_set_pos(ph, b.x, b.y);
    lv_obj_set_size(ph, b.w > 0 ? b.w : 40, b.h > 0 ? b.h : 40);
    lv_obj_set_style_bg_opa(ph, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(ph, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_border_width(ph, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ph, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(ph, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *build_splash(void) {
    font_cache_begin_pass();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ditto");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, font_cache_get(64), LV_PART_MAIN);
    lv_obj_center(title);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click, LV_EVENT_CLICKED, NULL);
    return scr;
}

static lv_obj_t *build_screen(dev_screen_t screen) {
    font_cache_begin_pass();
    if (!s_cfg || !s_cfg->valid) return build_splash();
    const cfg_screen_t *sc = &s_cfg->screens[screen];

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(s_cfg->brand_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click, LV_EVENT_CLICKED, NULL);

    int order[CFG_MAX_OBJECTS];
    cfg_sort_by_z(sc->objects, sc->n, order);
    for (int i = 0; i < sc->n; i++) {
        const cfg_object_t *o = &sc->objects[order[i]];
        switch (o->type) {
            case OBJ_TEXT: render_text(scr, o, s_cfg->brand_fg); break;
            case OBJ_QR:   render_qr(scr, o); break;
            default:       render_placeholder(scr, o, s_cfg->brand_fg); break;  // M5b/M5c
        }
    }
    add_status_dot(scr);
    return scr;
}

void ui_init(void) {
    size_t flen = 0;
    const uint8_t *ttf = assets_font(&flen);
    lvgl_port_lock(0);
    font_cache_init(ttf, flen);
    s_active = build_splash();
    lv_screen_load(s_active);
    lvgl_port_unlock();
}

void ui_set_config(const device_config_t *cfg) {
    lvgl_port_lock(0);
    s_cfg = cfg;
    lvgl_port_unlock();
}

void ui_render_state(dev_state_t st) {
    dev_screen_t screen = screen_for_state(st);
    lvgl_port_lock(0);
    s_status_dot = NULL;   // cleared each render; build_screen reinstalls it, splash leaves it NULL
    lv_obj_t *scr = (st == DEV_BOOT) ? build_splash() : build_screen(screen);
    lv_obj_t *old = s_active;
    s_active = scr;
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_delete(old);
    lvgl_port_unlock();
}

void ui_set_qr_url(const char *url) {
    lvgl_port_lock(0);
    if (!url) {
        s_qr_url[0] = '\0';
    } else {
        strncpy(s_qr_url, url, sizeof(s_qr_url) - 1);
        s_qr_url[sizeof(s_qr_url) - 1] = '\0';
    }
    lvgl_port_unlock();
}

void ui_set_online(bool online) {
    lvgl_port_lock(0);
    s_online = online;
    if (s_status_dot) {
        lv_obj_set_style_bg_color(s_status_dot,
            lv_color_hex(online ? DITTO_ONLINE : DITTO_GREY), LV_PART_MAIN);
    }
    lvgl_port_unlock();
}

bool ui_consume_tap(void) {
    bool t = s_tap_requested;
    s_tap_requested = false;
    return t;
}
