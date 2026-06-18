#include "ui.h"
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "draw/lv_draw_buf_private.h"   // lv_draw_buf_handlers_t fields (swap image alloc → PSRAM)
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "device_config.h"
#include "render_geom.h"
#include "font_cache.h"
#include "assets.h"
#include "asset_cache.h"
#include "clock_format.h"
#include "time_sync.h"

#define DITTO_GREEN   0x10A765
#define DITTO_ONLINE  0x37C871
#define DITTO_GREY    0x888888

static const device_config_t *s_cfg;
static lv_obj_t  *s_active;            // currently displayed screen
static lv_obj_t  *s_status_dot;        // chrome dot on the active screen
static char       s_qr_url[256];
static bool       s_online;
static volatile bool s_tap_requested;

static lv_obj_t  *s_clock_lbl;         // active-screen clock label (NULL if none)
static lv_timer_t *s_clock_timer;      // 1 Hz tick for the clock label
static bool       s_clock_24h;
static bool       s_clock_date;
static bool       s_clock_weekday;
static lv_obj_t  *s_wifi_obj;          // active-screen wifi widget (NULL if none)
static int        s_wifi_level = 0;

static void on_screen_click(lv_event_t *e) { (void)e; s_tap_requested = true; }

// --- PNG image decode → PSRAM ---------------------------------------------
// LVGL's lodepng decoder allocates the decoded ARGB8888 bitmap through the image
// draw-buf handlers (LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers). We route
// those allocations to PSRAM: a 720-reference logo can be ~hundreds of KB and
// internal DRAM is reserved for Wi-Fi/SDIO/TLS (see the M5a TLS fix). Installed
// once in ui_init under the LVGL lock; the align/stride/cache callbacks keep their
// defaults. (Image cache is disabled — CONFIG_LV_CACHE_DEF_SIZE=0 — so each draw
// decodes then frees this buffer; nothing lingers in PSRAM between draws.)
static void *img_psram_malloc(size_t size, lv_color_format_t cf)
{
    (void)cf;
    return heap_caps_malloc(size + LV_DRAW_BUF_ALIGN - 1, MALLOC_CAP_SPIRAM);
}
static void img_psram_free(void *buf) { heap_caps_free(buf); }

// Source bytes for one rendered image, freed when the lv_image object is deleted.
// We keep the encoded PNG + descriptor alive for the object's lifetime because the
// (uncached) decoder re-reads them on every redraw via the dsc src pointer.
typedef struct {
    lv_image_dsc_t dsc;
    uint8_t       *png;   // heap_caps PSRAM, owned here
} img_src_t;

static void on_image_delete(lv_event_t *e)
{
    img_src_t *s = (img_src_t *)lv_event_get_user_data(e);
    if (!s) return;
    heap_caps_free(s->png);
    heap_caps_free(s);
}

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

// Uploaded logo/icon images. URL comes from the top-level logo_url (OBJ_LOGO) or
// the object's icon_url (OBJ_ICON + ICON_UPLOAD). Render-thread safe: reads the
// asset cache-ONLY (asset_cache_read_cached, never networks) — a not-yet-prefetched
// or offline asset misses and falls back to the placeholder outline rather than
// blocking. On a hit, the cached PNG is wrapped as an in-memory LVGL image source;
// lodepng decodes it into PSRAM at draw time. Fit inside the geom box preserving
// aspect (LV_IMAGE_ALIGN_CONTAIN); icons with icon_circle get a circular clip.
static void render_image(lv_obj_t *scr, const cfg_object_t *o) {
    const char *url = (o->type == OBJ_LOGO) ? s_cfg->logo_url : o->icon_url;
    if (!url || url[0] == '\0') { render_placeholder(scr, o, s_cfg->brand_fg); return; }

    uint8_t *png = NULL;
    size_t   png_len = 0;
    if (asset_cache_read_cached(url, &png, &png_len) != ESP_OK || png_len < 8) {
        if (png) heap_caps_free(png);
        render_placeholder(scr, o, s_cfg->brand_fg);   // not cached yet → outline
        return;
    }

    img_src_t *s = heap_caps_malloc(sizeof(img_src_t), MALLOC_CAP_SPIRAM);
    if (!s) { heap_caps_free(png); render_placeholder(scr, o, s_cfg->brand_fg); return; }
    memset(s, 0, sizeof(*s));
    s->png = png;
    // Variable (in-memory) image source: data points at the encoded PNG bytes;
    // the lodepng decoder reads dims from the PNG header and decodes on draw.
    // header.magic keeps the first byte < 0x20 so LVGL treats this as a variable
    // source (not a file path / symbol).
    s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->dsc.data         = png;
    s->dsc.data_size    = png_len;

    px_box_t b = geom_box(o);
    lv_obj_t *img = lv_image_create(scr);
    lv_obj_set_pos(img, b.x, b.y);
    lv_obj_set_size(img, b.w > 0 ? b.w : 40, b.h > 0 ? b.h : 40);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);   // fit inside, keep aspect
    if (o->type == OBJ_ICON && o->icon_circle) {
        lv_obj_set_style_radius(img, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(img, true, LV_PART_MAIN);
    }
    lv_obj_add_event_cb(img, on_image_delete, LV_EVENT_DELETE, s);
    lv_image_set_src(img, &s->dsc);   // last: triggers decode-info while src lives
}

static void clock_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!s_clock_lbl) return;
    char buf[64];
    if (!time_is_synced()) {
        lv_label_set_text(s_clock_lbl, "--:--");
        return;
    }
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    format_clock(&lt, s_clock_24h, s_clock_date, s_clock_weekday, buf, sizeof(buf));
    lv_label_set_text(s_clock_lbl, buf);
}

static void render_clock(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_pos(lbl, b.x, b.y);
    lv_obj_set_width(lbl, b.w > 0 ? b.w : LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font_cache_get(o->font_size), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, lv_align_of(o->align), LV_PART_MAIN);
    s_clock_lbl     = lbl;
    s_clock_24h     = s_cfg->clock_24h;
    s_clock_date    = o->clock_show_date;
    s_clock_weekday = o->clock_show_weekday;
    clock_tick_cb(NULL);  // paint immediately
    s_clock_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
}

static void render_wifi(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_pos(cont, b.x, b.y);
    lv_obj_set_size(cont, b.w > 0 ? b.w : 40, b.h > 0 ? b.h : 24);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    int n = 4;
    int gap = 3;
    int barw = ((b.w > 0 ? b.w : 40) - gap * (n - 1)) / n;
    int maxh = (b.h > 0 ? b.h : 24);
    for (int i = 0; i < n; i++) {
        lv_obj_t *bar = lv_obj_create(cont);
        int bh = maxh * (i + 1) / n;
        lv_obj_set_size(bar, barw, bh);
        lv_obj_set_pos(bar, i * (barw + gap), maxh - bh);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 1, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, i < s_wifi_level ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }
    s_wifi_obj = cont;
}

static void render_spinner(lv_obj_t *scr, const cfg_object_t *o, uint32_t accent) {
    px_box_t b = geom_box(o);
    int side = (b.w < b.h ? b.w : b.h);
    if (side < 24) side = 24;
    lv_obj_t *sp = lv_spinner_create(scr);
    lv_obj_set_size(sp, side, side);
    lv_obj_set_pos(sp, b.x, b.y);
    lv_obj_set_style_arc_color(sp, lv_color_hex(accent), LV_PART_INDICATOR);
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
            case OBJ_LOGO: render_image(scr, o); break;
            case OBJ_ICON:
                if (o->icon_src == ICON_UPLOAD) render_image(scr, o);
                else render_placeholder(scr, o, s_cfg->brand_fg);  // preset sprites: M5c
                break;
            case OBJ_CLOCK:   render_clock(scr, o, s_cfg->brand_fg); break;
            case OBJ_WIFI:    render_wifi(scr, o, s_cfg->brand_fg); break;
            case OBJ_SPINNER: render_spinner(scr, o, s_cfg->brand_accent); break;
            default:       render_placeholder(scr, o, s_cfg->brand_fg); break;  // M5c remaining
        }
    }
    // Online status dot removed per product decision; connectivity will be shown
    // by the wifi widget (M5c). add_status_dot() retained but unused.
    (void)add_status_dot;
    return scr;
}

void ui_init(void) {
    size_t flen = 0;
    const uint8_t *ttf = assets_font(&flen);
    lvgl_port_lock(0);
    font_cache_init(ttf, flen);
    // Route decoded image bitmaps (lodepng output) to PSRAM. The widget/font
    // handlers are untouched.
    lv_draw_buf_handlers_t *ih = lv_draw_buf_get_image_handlers();
    ih->buf_malloc_cb = img_psram_malloc;
    ih->buf_free_cb   = img_psram_free;
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
    if (s_clock_timer) { lv_timer_delete(s_clock_timer); s_clock_timer = NULL; }
    s_status_dot = NULL;   // cleared each render; build_screen reinstalls it
    s_clock_lbl  = NULL;
    s_wifi_obj   = NULL;
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

void ui_set_wifi_level(int level) {
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    lvgl_port_lock(0);
    s_wifi_level = level;
    if (s_wifi_obj) {
        uint32_t childcnt = lv_obj_get_child_count(s_wifi_obj);
        for (uint32_t i = 0; i < childcnt; i++) {
            lv_obj_t *bar = lv_obj_get_child(s_wifi_obj, i);
            lv_obj_set_style_bg_opa(bar, (int)i < s_wifi_level ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
        }
    }
    lvgl_port_unlock();
}

bool ui_consume_tap(void) {
    bool t = s_tap_requested;
    s_tap_requested = false;
    return t;
}
