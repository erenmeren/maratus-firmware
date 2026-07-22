#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "lvgl.h"
#include "draw/lv_draw_buf_private.h"   // lv_draw_buf_handlers_t fields (swap image alloc → PSRAM)
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "device_config.h"
#include "render_geom.h"
#include "gesture.h"
#include "font_cache.h"
#include "assets.h"
#include "asset_cache.h"
#include "clock_format.h"
#include "time_sync.h"
#include "provisioning.h"

#define DITTO_GREEN   0x10A765
#define DITTO_ONLINE  0x37C871
#define DITTO_GREY    0x888888

static const char *TAG = "ui";

static const device_config_t *s_cfg;
static lv_obj_t  *s_active;            // currently displayed screen
static lv_obj_t  *s_status_dot;        // chrome dot on the active screen
static char       s_qr_url[512];       // matches device_config_t.pin_url / dev_command_t.url (512B)
static dev_state_t s_render_state = DEV_BOOT;  // last state passed to ui_render_state; OBJ_COUNTDOWN reads this
static char       s_pairing_code[16];
static bool       s_online;
static volatile bool s_tap_requested;
static volatile bool s_activity;

static lv_obj_t  *s_clock_time_lbl;    // active-screen clock time label (NULL if none)
static lv_obj_t  *s_clock_date_lbl;    // active-screen clock date label (NULL if hidden/none)
static lv_timer_t *s_clock_timer;      // 1 Hz tick for the clock labels
static bool       s_clock_24h;
static bool       s_clock_date;
static bool       s_clock_weekday;
static lv_obj_t  *s_wifi_obj;          // active-screen wifi widget (NULL if none)
static int        s_wifi_level = 0;

static lv_obj_t  *s_cd_time;           // "M:SS" remaining label (NULL if none)
static lv_obj_t  *s_cd_bar;            // progress bar (NULL if none)
static lv_timer_t *s_cd_timer;         // 1 Hz tick
static int        s_cd_pending = 60;   // duration the next render should arm
static int        s_cd_remaining;      // seconds left on the active widget
static bool       s_cd_expired;        // set at 0, consumed by the state machine

static lv_obj_t  *s_boot_bar;          // splash progress bar (NULL off-splash)
static lv_obj_t  *s_boot_lbl;          // splash status label (NULL off-splash)

typedef struct { int pct; const char *label; } boot_stage_info_t;
static const boot_stage_info_t BOOT_STAGES[] = {
    [BOOT_STARTING] = { 10,  "Starting up..." },
    [BOOT_CONFIG]   = { 25,  "Loading saved settings..." },
    [BOOT_WIFI]     = { 40,  "Connecting to Wi-Fi..." },
    [BOOT_CLOUD]    = { 70,  "Contacting Ditto..." },
    [BOOT_READY]    = { 100, "Ready" },
    [BOOT_OFFLINE]  = { -1,  "Starting offline..." },   // label-only
};

#define SWIPE_UP_MIN_PX 150
static volatile bool s_swipeup = false;            // set on a swipe-up release; consumed by poll_task
static int  s_press_x, s_press_y;                  // touch-down coords for the swipe test
static bool s_press_valid = false;                 // a usable press is in flight
static bool s_suppress_click = false;              // eat the CLICKED that ends a swipe
static volatile bool s_input_suppressed = false;   // set by app_state during sleep / wake-guard

// Live config pointer (set by ui_set_config); ui_settings colors itself with it.
const device_config_t *ui_current_cfg(void) { return s_cfg; }

static void on_screen_press(lv_event_t *e) {
    (void)e;
    s_activity = true;
    s_suppress_click = false;
    if (s_input_suppressed) { s_press_valid = false; return; }   // a waking/guarded touch can't open settings
    lv_indev_t *indev = lv_indev_active();
    if (indev) { lv_point_t p; lv_indev_get_point(indev, &p); s_press_x = p.x; s_press_y = p.y; s_press_valid = true; }
    else s_press_valid = false;
}

static void on_screen_release(lv_event_t *e) {
    (void)e;
    if (!s_press_valid) return;
    s_press_valid = false;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    if (gesture_is_swipe_up(s_press_x, s_press_y, p.x, p.y, SWIPE_UP_MIN_PX)) {
        s_swipeup = true;
        s_suppress_click = true;   // eat the trailing CLICKED that ends the swipe
    }
}

static void on_screen_click(lv_event_t *e) {
    (void)e;
    if (s_suppress_click) { s_suppress_click = false; return; }
    s_tap_requested = true;
}

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
    lv_obj_set_style_text_font(lbl, font_cache_get(o->font_size, false), LV_PART_MAIN);
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

// Uploaded image/icon assets. URL comes from image_url (OBJ_IMAGE) or icon_url
// (OBJ_ICON + ICON_UPLOAD). OBJ_LOGO no longer routes here — it renders as a
// text wordmark via render_wordmark. Render-thread safe: reads the asset cache-ONLY
// (asset_cache_read_cached, never networks) — a not-yet-prefetched or offline asset
// misses and falls back to the placeholder outline rather than blocking. On a hit,
// the cached PNG is wrapped as an in-memory LVGL image source; lodepng decodes it
// into PSRAM at draw time. Fit inside the geom box preserving aspect
// (LV_IMAGE_ALIGN_CONTAIN); icons with icon_circle get a circular clip.
// Diagnostic: state of the last OBJ_IMAGE render, reported on the heartbeat.
// -1000 none yet · -2 empty url · 0 cache miss (placeholder) · 1 cache hit (decoded).
static volatile int s_last_img_state = -1000;
int ui_last_image_state(void) { return s_last_img_state; }

// Number of OBJ_IMAGE objects in the LIVE applied config (all screens). -1 if
// no valid config is applied. Reported on the heartbeat so we can tell whether
// the config the device is actually running even contains the branding image.
int ui_live_image_count(void) {
    if (!s_cfg || !s_cfg->valid) return -1;
    int n = 0;
    for (int s = 0; s < SCREEN_COUNT; s++)
        for (int i = 0; i < s_cfg->screens[s].n; i++)
            if (s_cfg->screens[s].objects[i].type == OBJ_IMAGE) n++;
    return n;
}

static void render_image(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    const bool is_img = (o->type == OBJ_IMAGE);
    const char *url = is_img ? o->image_url : o->icon_url;
    if (!url || url[0] == '\0') { if (is_img) s_last_img_state = -2; render_placeholder(scr, o, fg); return; }

    uint8_t *png = NULL;
    size_t   png_len = 0;
    if (asset_cache_read_cached(url, &png, &png_len) != ESP_OK || png_len < 8) {
        if (png) heap_caps_free(png);
        if (is_img) s_last_img_state = 0;
        render_placeholder(scr, o, fg);   // not cached yet → outline
        return;
    }
    if (is_img) s_last_img_state = 1;

    img_src_t *s = heap_caps_malloc(sizeof(img_src_t), MALLOC_CAP_SPIRAM);
    if (!s) { heap_caps_free(png); render_placeholder(scr, o, fg); return; }
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

// The logo widget renders the brand wordmark (top-level "wordmark") as centered
// bold brand-fg text — matches the admin preview's LogoObject. Empty wordmark = no-op.
static void render_wordmark(lv_obj_t *scr, const cfg_object_t *o) {
    if (!s_cfg->wordmark[0]) return;
    px_box_t b = geom_box(o);
    int px = (int)(o->h * 720.0f * 0.42f);
    if (px < 8) px = 8;
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, b.w > 0 ? b.w : LV_SIZE_CONTENT);
    lv_label_set_text(lbl, s_cfg->wordmark);
    lv_obj_set_style_text_color(lbl, lv_color_hex(s_cfg->brand_fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font_cache_get(px, /*bold*/ true), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, b.x, b.y);
}

static char s_clock_time_last[32];     // last text pushed to the time label
static char s_clock_date_last[48];     // last text pushed to the date label

static void clock_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!s_clock_time_lbl) return;
    char tbuf[32], dbuf[48];
    if (!time_is_synced()) {
        strncpy(tbuf, "--:--", sizeof(tbuf) - 1);
        tbuf[sizeof(tbuf) - 1] = '\0';
        dbuf[0] = '\0';
    } else {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        format_clock_time(&lt, s_clock_24h, tbuf, sizeof(tbuf));
        if (s_clock_date) format_clock_date(&lt, s_clock_weekday, dbuf, sizeof(dbuf));
        else              dbuf[0] = '\0';
    }
    // Time (HH:MM) and date change at most once a minute, but this tick fires every
    // second. lv_label_set_text always invalidates+redraws (no equality check), so
    // repaint each label only when its own text actually changed.
    if (strcmp(tbuf, s_clock_time_last) != 0) {
        strncpy(s_clock_time_last, tbuf, sizeof(s_clock_time_last) - 1);
        s_clock_time_last[sizeof(s_clock_time_last) - 1] = '\0';
        lv_label_set_text(s_clock_time_lbl, tbuf);
    }
    if (s_clock_date_lbl && strcmp(dbuf, s_clock_date_last) != 0) {
        strncpy(s_clock_date_last, dbuf, sizeof(s_clock_date_last) - 1);
        s_clock_date_last[sizeof(s_clock_date_last) - 1] = '\0';
        lv_label_set_text(s_clock_date_lbl, dbuf);
    }
}

// Matches the branding preview's clock: a bold time (~half the box height) above a
// smaller, muted date line, the whole block vertically centered in the box. Admin
// sends no fontSize for the clock, so the time size is derived from the box height.
static void render_clock(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    bool show_date = o->clock_show_date;

    int box_h   = b.h > 0 ? b.h : 80;
    int time_px = o->font_size > 0 ? o->font_size : box_h / 2;   // preview: time ~ box_h * 0.5
    if (time_px < 12) time_px = 12;
    int date_px = (time_px * 26) / 100;                          // preview: date ~ 26% of time
    if (date_px < 10) date_px = 10;
    int gap     = (time_px * 10) / 100;                          // small gap between the lines

    const lv_font_t *time_font = font_cache_get(time_px, /*bold*/ true);
    const lv_font_t *date_font = show_date ? font_cache_get(date_px, /*bold*/ false) : NULL;
    int time_lh = lv_font_get_line_height(time_font);
    int date_lh = date_font ? lv_font_get_line_height(date_font) : 0;
    int total_h = time_lh + (show_date ? gap + date_lh : 0);
    int top     = b.y + (box_h - total_h) / 2;
    if (top < b.y) top = b.y;
    int width   = b.w > 0 ? b.w : LV_SIZE_CONTENT;
    lv_text_align_t ta = lv_align_of(o->align);

    lv_obj_t *time_lbl = lv_label_create(scr);
    lv_obj_set_width(time_lbl, width);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(time_lbl, time_font, LV_PART_MAIN);
    lv_obj_set_style_text_align(time_lbl, ta, LV_PART_MAIN);
    lv_obj_set_pos(time_lbl, b.x, top);
    s_clock_time_lbl = time_lbl;

    s_clock_date_lbl = NULL;
    if (show_date) {
        lv_obj_t *date_lbl = lv_label_create(scr);
        lv_obj_set_width(date_lbl, width);
        lv_obj_set_style_text_color(date_lbl, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_text_opa(date_lbl, LV_OPA_60, LV_PART_MAIN);  // muted (no separate muted color on device)
        lv_obj_set_style_text_font(date_lbl, date_font, LV_PART_MAIN);
        lv_obj_set_style_text_align(date_lbl, ta, LV_PART_MAIN);
        lv_obj_set_pos(date_lbl, b.x, top + time_lh + gap);
        s_clock_date_lbl = date_lbl;
    }

    s_clock_24h     = s_cfg->clock_24h;
    s_clock_date    = show_date;
    s_clock_weekday = o->clock_show_weekday;
    s_clock_time_last[0] = '\0';   // new labels: force the first paint
    s_clock_date_last[0] = '\0';
    clock_tick_cb(NULL);  // paint immediately
    if (s_clock_timer) { lv_timer_delete(s_clock_timer); s_clock_timer = NULL; }
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
    // Bar proportions match the branding preview's WifiObject (ditto-admin
    // components/device-preview/printer-preview.tsx): heights 0.45/0.65/0.85/1
    // of the base (min of container w/h), bar width ~0.22x base, gap ~0.14x
    // base, corner radius ~0.10x base, active ~85% opacity / inactive ~20%.
    const float heights[4] = {0.45f, 0.65f, 0.85f, 1.0f};
    int base = (b.w > 0 ? b.w : 40); if ((b.h > 0 ? b.h : 24) < base) base = (b.h > 0 ? b.h : 24);
    int barw = (int)(base * 0.22f); if (barw < 1) barw = 1;
    int gap  = (int)(base * 0.14f); if (gap < 1) gap = 1;
    int rad  = (int)(base * 0.10f);
    int maxh = (b.h > 0 ? b.h : 24);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(cont);
        int bh = (int)(maxh * heights[i]);
        lv_obj_set_size(bar, barw, bh);
        lv_obj_set_pos(bar, i * (barw + gap), maxh - bh);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, rad, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, i < s_wifi_level ? (lv_opa_t)217 : (lv_opa_t)51, LV_PART_MAIN); // ~85% / ~20%
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
    lv_obj_set_style_arc_color(sp, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(sp,   LV_OPA_30,            LV_PART_MAIN);
}

static void cd_tick_cb(lv_timer_t *t) {
    (void)t;
    if (s_cd_remaining > 0) {
        s_cd_remaining--;
        if (s_cd_time) {
            char buf[16];
            format_mmss(s_cd_remaining, buf, sizeof(buf));
            lv_label_set_text(s_cd_time, buf);
        }
        if (s_cd_bar) lv_bar_set_value(s_cd_bar, s_cd_remaining, LV_ANIM_OFF);
        if (s_cd_remaining == 0) s_cd_expired = true;
    }
}

static void render_countdown(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg, uint32_t accent) {
    px_box_t b = geom_box(o);
    int total = s_cd_pending;
    s_cd_remaining = total;
    int box_w = b.w > 0 ? b.w : 160;
    const lv_font_t *font = font_cache_get(o->font_size > 0 ? o->font_size : 18, false);

    char buf[16];
    format_mmss(total, buf, sizeof(buf));

    // Caption ("Code expires", left) and time ("M:SS", right) share one row. On a
    // box too narrow to fit both they would overlap, so only show the caption when
    // there's room; otherwise center the time on its own.
    int cap_w  = lv_text_get_width("Code expires", 12, font, 0);
    int time_w = lv_text_get_width("00:00", 5, font, 0);
    bool show_caption = box_w >= cap_w + time_w + 12;

    if (show_caption) {
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "Code expires");
        lv_obj_set_style_text_color(label, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_pos(label, b.x, b.y);
    }

    lv_obj_t *tlbl = lv_label_create(scr);
    lv_label_set_text(tlbl, buf);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(tlbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(tlbl, show_caption ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(tlbl, box_w);
    lv_obj_set_pos(tlbl, b.x, b.y);
    s_cd_time = tlbl;

    lv_obj_t *bar = lv_bar_create(scr);
    int barh = 8;
    lv_obj_set_size(bar, b.w > 0 ? b.w : 160, barh);
    lv_obj_set_pos(bar, b.x, b.y + (b.h > 0 ? b.h - barh : 28));
    lv_bar_set_range(bar, 0, total > 0 ? total : 1);
    lv_bar_set_value(bar, total, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(bar, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    s_cd_bar = bar;

    /* only non-NULL if this screen defines >1 OBJ_COUNTDOWN; ui_render_state already NULL'd it otherwise */
    if (s_cd_timer) { lv_timer_delete(s_cd_timer); s_cd_timer = NULL; }
    s_cd_timer = lv_timer_create(cd_tick_cb, 1000, NULL);
}

// Setup-screen pairing code: a large, letter-spaced, centered code (e.g.
// "ABCD-2345") the operator types into the dashboard. Falls back to "----" until
// a code has been set via ui_set_pairing_code.
static void render_pairing_code(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    int box_h = b.h > 0 ? b.h : 80;
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_width(lbl, b.w > 0 ? b.w : LV_SIZE_CONTENT);
    lv_label_set_text(lbl, s_pairing_code[0] ? s_pairing_code : "----");
    lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font_cache_get(box_h * 2 / 3, true), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(lbl, 2, LV_PART_MAIN);
    lv_obj_set_pos(lbl, b.x, b.y);
}

static const char *const SETUP_STEPS[] = {
    "1.  Open your Ditto dashboard",
    "2.  Add a printer to a store",
    "3.  Enter the code below",
};
static void render_steps(lv_obj_t *scr, const cfg_object_t *o, uint32_t fg) {
    px_box_t b = geom_box(o);
    int n = (int)(sizeof(SETUP_STEPS) / sizeof(SETUP_STEPS[0]));
    int fs = o->font_size > 0 ? o->font_size : 22;
    int line_h = (b.h > 0 ? b.h : 120) / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_obj_set_width(lbl, b.w > 0 ? b.w : LV_SIZE_CONTENT);
        lv_label_set_text(lbl, SETUP_STEPS[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, font_cache_get(fs, false), LV_PART_MAIN);
        lv_obj_set_pos(lbl, b.x, b.y + i * line_h);
    }
}

static lv_obj_t *build_splash(bool with_progress) {
    font_cache_begin_pass();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(DITTO_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ditto");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, font_cache_get(64, /*bold*/ true), LV_PART_MAIN);
    lv_obj_center(title);
    if (with_progress) {
        lv_obj_t *bar = lv_bar_create(scr);
        lv_obj_set_size(bar, 320, 8);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_30, LV_PART_MAIN);          // track: white @ ~25%
        lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);  // indicator: solid white
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 80);
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_opa(lbl, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, font_cache_get(20, false), LV_PART_MAIN);
        lv_obj_align_to(lbl, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
        s_boot_bar = bar;
        s_boot_lbl = lbl;
    }
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click,   LV_EVENT_CLICKED,  NULL);
    lv_obj_add_event_cb(scr, on_screen_press,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, on_screen_release, LV_EVENT_RELEASED, NULL);
    return scr;
}

static lv_obj_t *build_screen(dev_screen_t screen) {
    font_cache_begin_pass();
    if (!s_cfg || !s_cfg->valid) return build_splash(false);
    const cfg_screen_t *sc = &s_cfg->screens[screen];

    // Effective palette: the screen's override when present, else global brand.
    const uint32_t bg     = sc->has_colors ? sc->col_bg     : s_cfg->brand_bg;
    const uint32_t fg     = sc->has_colors ? sc->col_fg     : s_cfg->brand_fg;
    const uint32_t accent = sc->has_colors ? sc->col_accent : s_cfg->brand_accent;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_click,   LV_EVENT_CLICKED,  NULL);
    lv_obj_add_event_cb(scr, on_screen_press,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, on_screen_release, LV_EVENT_RELEASED, NULL);

    int order[CFG_MAX_OBJECTS];
    cfg_sort_by_z(sc->objects, sc->n, order);
    for (int i = 0; i < sc->n; i++) {
        const cfg_object_t *o = &sc->objects[order[i]];
        switch (o->type) {
            case OBJ_TEXT:  render_text(scr, o, fg); break;
            case OBJ_QR:    render_qr(scr, o); break;
            case OBJ_LOGO:  render_wordmark(scr, o); break;
            case OBJ_IMAGE: render_image(scr, o, fg); break;
            case OBJ_ICON:
                if (o->icon_url[0]) render_image(scr, o, fg);  // legacy: treat as an image
                break;
            case OBJ_CLOCK:   render_clock(scr, o, fg); break;
            case OBJ_WIFI:    render_wifi(scr, o, fg); break;
            case OBJ_SPINNER:   render_spinner(scr, o, accent); break;
            case OBJ_COUNTDOWN: if (s_render_state != DEV_PINNED) render_countdown(scr, o, fg, accent); break;
            case OBJ_PAIRING_CODE: render_pairing_code(scr, o, fg); break;
            case OBJ_STEPS:        render_steps(scr, o, fg); break;
            default:       render_placeholder(scr, o, fg); break;  // M5c remaining
        }
    }
    // Online status dot removed per product decision; connectivity will be shown
    // by the wifi widget (M5c). add_status_dot() retained but unused.
    (void)add_status_dot;

    // Setup screen: render the device serial in small muted type at the very
    // bottom, so a support call can read it off even once the unit is mounted
    // and the label QR is unreachable. Drawn directly (not a config object) so
    // it can't be overridden by any config and stays immune to layout drift.
    if (screen == SCREEN_SETUP) {
        char serial[13];
        provisioning_get_serial(serial);
        if (serial[0]) {
            char buf[20];
            snprintf(buf, sizeof(buf), "SN %s", serial);
            lv_obj_t *lbl = lv_label_create(scr);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
            lv_obj_set_style_text_opa(lbl, LV_OPA_60, LV_PART_MAIN);  // muted (no separate muted color on device)
            lv_obj_set_style_text_font(lbl, font_cache_get(14, false), LV_PART_MAIN);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
        }
    }
    return scr;
}

void ui_init(void) {
    size_t reg_len = 0, bold_len = 0;
    const uint8_t *reg  = assets_font(&reg_len);
    const uint8_t *bold = assets_font_bold(&bold_len);
    lvgl_port_lock(0);
    font_cache_init(reg, reg_len, bold, bold_len);
    // Route decoded image bitmaps (lodepng output) to PSRAM. The widget/font
    // handlers are untouched.
    lv_draw_buf_handlers_t *ih = lv_draw_buf_get_image_handlers();
    ih->buf_malloc_cb = img_psram_malloc;
    ih->buf_free_cb   = img_psram_free;
    s_active = build_splash(true);
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
    s_render_state = st;   // build_screen reads this to suppress the countdown when pinned
    if (s_clock_timer) { lv_timer_delete(s_clock_timer); s_clock_timer = NULL; }
    if (s_cd_timer)   { lv_timer_delete(s_cd_timer);   s_cd_timer   = NULL; }
    s_status_dot = NULL;   // cleared each render; build_screen reinstalls it
    s_clock_time_lbl = NULL;
    s_clock_date_lbl = NULL;
    s_wifi_obj   = NULL;
    s_cd_expired = false;   // re-render restarts the countdown; a pending expiry is stale
    s_cd_time    = NULL;
    s_cd_bar     = NULL;
    s_boot_bar = NULL;   // cleared each render; build_splash(true) reinstalls
    s_boot_lbl = NULL;
    lv_obj_t *scr = (st == DEV_BOOT) ? build_splash(true) : build_screen(screen);
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
        if (strlen(url) > sizeof(s_qr_url) - 1) {
            ESP_LOGW(TAG, "qr url len=%d exceeds s_qr_url capacity (%d); truncating", (int)strlen(url), (int)sizeof(s_qr_url) - 1);
        }
        strncpy(s_qr_url, url, sizeof(s_qr_url) - 1);
        s_qr_url[sizeof(s_qr_url) - 1] = '\0';
    }
    lvgl_port_unlock();
}

void ui_set_pairing_code(const char *code) {
    lvgl_port_lock(0);
    if (!code) s_pairing_code[0] = '\0';
    else { strncpy(s_pairing_code, code, sizeof(s_pairing_code) - 1); s_pairing_code[sizeof(s_pairing_code) - 1] = '\0'; }
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
    // The poll task pushes the level every cycle. lv_obj_set_style_bg_opa always
    // invalidates+redraws the bar (no equality check in LVGL), so repainting the
    // bars on every poll is wasted work when the level is unchanged. Only touch the
    // bars when the level actually changed. render_wifi paints from s_wifi_level
    // directly when a screen is (re)built, so a skipped no-op update is safe.
    if (level == s_wifi_level) { lvgl_port_unlock(); return; }
    s_wifi_level = level;
    if (s_wifi_obj) {
        uint32_t childcnt = lv_obj_get_child_count(s_wifi_obj);
        for (uint32_t i = 0; i < childcnt; i++) {
            lv_obj_t *bar = lv_obj_get_child(s_wifi_obj, i);
            lv_obj_set_style_bg_opa(bar, (int)i < s_wifi_level ? (lv_opa_t)217 : (lv_opa_t)51, LV_PART_MAIN); // ~85% / ~20%
        }
    }
    lvgl_port_unlock();
}

void ui_set_countdown(int seconds) {
    if (seconds < 15) seconds = 15;
    if (seconds > 180) seconds = 180;
    lvgl_port_lock(0);
    s_cd_pending = seconds;
    s_cd_expired = false;
    lvgl_port_unlock();
}

bool ui_consume_countdown_expired(void) {
    lvgl_port_lock(0);
    bool e = s_cd_expired;
    s_cd_expired = false;
    lvgl_port_unlock();
    return e;
}

bool ui_consume_tap(void) {
    lvgl_port_lock(0);
    bool t = s_tap_requested;
    s_tap_requested = false;
    lvgl_port_unlock();
    return t;
}

bool ui_consume_activity(void) {
    bool v;
    lvgl_port_lock(0);
    v = s_activity;
    s_activity = false;
    lvgl_port_unlock();
    return v;
}

bool ui_consume_swipeup(void) {                 // consume+clear (poll_task)
    lvgl_port_lock(0);
    bool v = s_swipeup;
    s_swipeup = false;
    lvgl_port_unlock();
    return v;
}

bool ui_swipeup_pending(void) {                 // non-consuming peek (idle_wait early-exit)
    lvgl_port_lock(0);
    bool v = s_swipeup;
    lvgl_port_unlock();
    return v;
}

void ui_set_input_suppressed(bool s) {
    lvgl_port_lock(0);
    s_input_suppressed = s;
    lvgl_port_unlock();
}

void ui_boot_progress(boot_stage_t stage) {
    if ((unsigned)stage > BOOT_OFFLINE) return;   // enum may be unsigned; single-sided guard
    lvgl_port_lock(0);
    if (s_boot_bar && s_boot_lbl) {
        const boot_stage_info_t *si = &BOOT_STAGES[stage];
        if (si->pct >= 0) lv_bar_set_value(s_boot_bar, si->pct, LV_ANIM_ON);
        lv_label_set_text(s_boot_lbl, si->label);
    }
    lvgl_port_unlock();
}
