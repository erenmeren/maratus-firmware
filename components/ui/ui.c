#include "ui.h"
#include "lvgl.h"

// Brand palette (mirrors ditto-admin defaults: brandColor #10A765).
#define DITTO_GREEN   0x10A765
#define DITTO_ONLINE  0x37C871
#define DITTO_GREY    0x888888

static lv_obj_t *s_idle_screen;
static lv_obj_t *s_status_dot;

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

    return scr;
}

void ui_init(void)
{
    s_idle_screen = make_idle_screen();
}

void ui_show(ui_screen_t screen)
{
    // M2: only IDLE exists; later milestones add the rest.
    if (screen == UI_SCREEN_IDLE && s_idle_screen) {
        lv_screen_load(s_idle_screen);
    }
}

void ui_set_online(bool online)
{
    if (s_status_dot) {
        lv_obj_set_style_bg_color(s_status_dot,
            lv_color_hex(online ? DITTO_ONLINE : DITTO_GREY), LV_PART_MAIN);
    }
}
