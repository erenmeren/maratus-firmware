#include "cfg_parse.h"
#include <string.h>
#include "cJSON.h"

static const char *SCREEN_NAMES[SCREEN_COUNT] = {
    "idle", "processing", "qr", "sent", "error", "paused", "setup", "pinned"
};
_Static_assert(
    sizeof(SCREEN_NAMES) / sizeof(SCREEN_NAMES[0]) == SCREEN_COUNT,
    "SCREEN_NAMES must match dev_screen_t");

static uint32_t parse_hex_color(const char *s, uint32_t dflt) {
    if (!s) return dflt;
    if (*s == '#') s++;
    // Accept exactly 6 hex digits; any suffix (e.g. 8-char RGBA) is ignored.
    if (strlen(s) < 6) return dflt;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i]; int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return dflt;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

/* Strict: true only if s is 6 hex digits (optionally '#'-prefixed); no default. */
static bool parse_hex_color_strict(const char *s, uint32_t *out) {
    if (!s) return false;
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i]; int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}

static cfg_align_t align_of(const char *s) {
    if (s && strcmp(s, "center") == 0) return CFG_ALIGN_CENTER;
    if (s && strcmp(s, "right")  == 0) return CFG_ALIGN_RIGHT;
    return CFG_ALIGN_LEFT;
}

static cfg_obj_type_t type_of(const char *s) {
    if (!s) return OBJ_UNKNOWN;
    if (!strcmp(s, "text"))        return OBJ_TEXT;
    if (!strcmp(s, "icon"))        return OBJ_ICON;
    if (!strcmp(s, "logo"))        return OBJ_LOGO;
    if (!strcmp(s, "clock"))       return OBJ_CLOCK;
    if (!strcmp(s, "wifi"))        return OBJ_WIFI;
    if (!strcmp(s, "qr"))          return OBJ_QR;
    if (!strcmp(s, "spinner"))     return OBJ_SPINNER;
    if (!strcmp(s, "countdown"))   return OBJ_COUNTDOWN;
    if (!strcmp(s, "pairingCode")) return OBJ_PAIRING_CODE;
    if (!strcmp(s, "steps"))       return OBJ_STEPS;
    if (!strcmp(s, "image"))       return OBJ_IMAGE;
    return OBJ_UNKNOWN;
}

static qr_shape_t qr_shape_of(const char *s) {
    if (s && !strcmp(s, "classic")) return QR_SHAPE_CLASSIC;
    if (s && !strcmp(s, "soft"))    return QR_SHAPE_SOFT;
    if (s && !strcmp(s, "dots"))    return QR_SHAPE_DOTS;
    return QR_SHAPE_ROUNDED;   // default: absent, unknown, or explicit "rounded"
}

// config.qrCornerRadius: 0..100 int slider (0 = square), replacing the
// earlier 2-value `qrCorner: "square" | "rounded"` enum (2026-07-24 slider
// addendum). Mirrors ditto-admin lib/printer-layout.ts sanitizeQrCornerRadius:
// a valid stored qrCornerRadius number always wins (clamped+rounded to
// 0..100); otherwise a legacy qrCorner "rounded" -> 24, "square" -> 0;
// anything else (absent, garbage, unrecognized legacy value) -> the default
// (24, today's live look).
static int qr_corner_radius_of(cJSON *config) {
    cJSON *v = config ? cJSON_GetObjectItem(config, "qrCornerRadius") : NULL;
    if (cJSON_IsNumber(v)) {
        int r = (int)(v->valuedouble + 0.5);
        if (r < 0)   r = 0;
        if (r > 100) r = 100;
        return r;
    }
    const char *legacy = config ? cJSON_GetStringValue(cJSON_GetObjectItem(config, "qrCorner")) : NULL;
    if (legacy && !strcmp(legacy, "rounded")) return 24;
    if (legacy && !strcmp(legacy, "square"))  return 0;
    return 24;   // default: absent, garbage, or an unrecognized legacy value
}

static qr_shadow_mode_t qr_shadow_mode_of(const char *s) {
    if (s && !strcmp(s, "drop")) return QR_SHADOW_DROP;
    if (s && !strcmp(s, "neon")) return QR_SHADOW_NEON;
    return QR_SHADOW_NONE;   // default: absent, unknown, or explicit "none"
}

static cfg_tint_t tint_of(const char *s) {
    if (s && !strcmp(s, "accent")) return TINT_ACCENT;
    if (s && !strcmp(s, "muted"))  return TINT_MUTED;
    if (s && !strcmp(s, "warn"))   return TINT_WARN;
    return TINT_NONE;
}

static float fnum(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1); dst[cap - 1] = '\0';
}

static void parse_screen(cJSON *objs, cfg_screen_t *out) {
    out->n = 0;
    int n = cJSON_IsArray(objs) ? cJSON_GetArraySize(objs) : 0;
    for (int i = 0; i < n && out->n < CFG_MAX_OBJECTS; i++) {
        cJSON *o = cJSON_GetArrayItem(objs, i);
        cJSON *vis = cJSON_GetObjectItem(o, "visible");
        if (cJSON_IsBool(vis) && !cJSON_IsTrue(vis)) continue;   // skip hidden
        cfg_obj_type_t type = type_of(cJSON_GetStringValue(cJSON_GetObjectItem(o, "type")));
        if (type == OBJ_UNKNOWN) continue;                        // drop unknowns

        cfg_object_t *t = &out->objects[out->n++];
        memset(t, 0, sizeof(*t));
        t->type = type;
        t->visible = true;
        t->x = fnum(o, "x"); t->y = fnum(o, "y");
        t->w = fnum(o, "w"); t->h = fnum(o, "h");
        cJSON *z = cJSON_GetObjectItem(o, "z");
        t->z = cJSON_IsNumber(z) ? (int)z->valuedouble : 0;

        if (type == OBJ_TEXT) {
            copy_str(t->text, CFG_TEXT_LEN, cJSON_GetStringValue(cJSON_GetObjectItem(o, "text")));
            cJSON *fs = cJSON_GetObjectItem(o, "fontSize");
            t->font_size = cJSON_IsNumber(fs) ? (int)fs->valuedouble : 28;
            if (t->font_size < 8)   t->font_size = 8;
            if (t->font_size > 160) t->font_size = 160;
            t->align = align_of(cJSON_GetStringValue(cJSON_GetObjectItem(o, "align")));
        } else if (type == OBJ_ICON) {
            cJSON *icon = cJSON_GetObjectItem(o, "icon");
            const char *src = icon ? cJSON_GetStringValue(cJSON_GetObjectItem(icon, "source")) : NULL;
            t->icon_src = (src && !strcmp(src, "upload")) ? ICON_UPLOAD : ICON_PRESET;
            copy_str(t->icon_preset, CFG_PRESET_LEN, icon ? cJSON_GetStringValue(cJSON_GetObjectItem(icon, "preset")) : NULL);
            copy_str(t->icon_url, CFG_URL_LEN, icon ? cJSON_GetStringValue(cJSON_GetObjectItem(icon, "signedUrl")) : NULL);
            t->icon_tint = tint_of(icon ? cJSON_GetStringValue(cJSON_GetObjectItem(icon, "tint")) : NULL);
            cJSON *circ = icon ? cJSON_GetObjectItem(icon, "circle") : NULL;
            t->icon_circle = cJSON_IsTrue(circ);
        } else if (type == OBJ_IMAGE) {
            cJSON *image = cJSON_GetObjectItem(o, "image");
            copy_str(t->image_url, CFG_URL_LEN,
                     image ? cJSON_GetStringValue(cJSON_GetObjectItem(image, "signedUrl")) : NULL);
        } else if (type == OBJ_CLOCK) {
            // Admin/preview default the clock alignment to "center"; honor a stored
            // align but default to center (not the align_of() left fallback) so the
            // device matches the branding preview.
            cJSON *al = cJSON_GetObjectItem(o, "align");
            t->align = al ? align_of(cJSON_GetStringValue(al)) : CFG_ALIGN_CENTER;
            cJSON *clk = cJSON_GetObjectItem(o, "clock");
            cJSON *sd = clk ? cJSON_GetObjectItem(clk, "showDate") : NULL;
            cJSON *sw = clk ? cJSON_GetObjectItem(clk, "showWeekday") : NULL;
            t->clock_show_date    = sd ? cJSON_IsTrue(sd) : true;
            t->clock_show_weekday = sw ? cJSON_IsTrue(sw) : true;
        }
    }
}

bool cfg_parse_json(const char *json, device_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));   // safe defaults; valid stays false until we finish
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    const char *ver = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    copy_str(cfg->etag, sizeof(cfg->etag), ver);

    cfg->brand_bg     = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandBg")),     0xE8A33D);  // Maratus amber
    // Fallback is effectively dead today: ditto-admin's resolveBrandTokens (lib/color.ts)
    // always emits explicit 6-hex brand colors in every config push. Keep it contract-safe
    // if that ever changes.
    cfg->brand_fg     = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandFg")),     0x111827);  // Maratus ink (white-on-amber-default is low contrast)
    cfg->brand_accent = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandColor")),  0x111827);  // Maratus ink (contrasts against the amber default bg)
    cfg->brand_muted  = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandMuted")),  0x9CA3AF);
    cfg->brand_warn   = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandWarn")),   0xF59E0B);
    copy_str(cfg->logo_url, CFG_URL_LEN, cJSON_GetStringValue(cJSON_GetObjectItem(root, "logoUrl")));
    copy_str(cfg->wordmark, CFG_TEXT_LEN, cJSON_GetStringValue(cJSON_GetObjectItem(root, "wordmark")));

    cJSON *config = cJSON_GetObjectItem(root, "config");
    copy_str(cfg->clock_timezone, CFG_TZ_LEN,
             config ? cJSON_GetStringValue(cJSON_GetObjectItem(config, "clockTimezone")) : NULL);
    cfg->clock_24h = config ? cJSON_IsTrue(cJSON_GetObjectItem(config, "clock24h")) : false;
    cJSON *wl = config ? cJSON_GetObjectItem(config, "wifiLevel") : NULL;
    cfg->wifi_level = cJSON_IsNumber(wl) ? (int)wl->valuedouble : 0;
    if (cfg->wifi_level < 0) cfg->wifi_level = 0;
    if (cfg->wifi_level > 4) cfg->wifi_level = 4;

    cJSON *qt = config ? cJSON_GetObjectItem(config, "qrTimeoutSeconds") : NULL;
    cfg->qr_timeout_seconds = cJSON_IsNumber(qt) ? (int)qt->valuedouble : 60;
    if (cfg->qr_timeout_seconds < 5)   cfg->qr_timeout_seconds = 5;
    if (cfg->qr_timeout_seconds > 180) cfg->qr_timeout_seconds = 180;

    // QR style: qrShape/qrFg/qrBg live inside "config", siblings of
    // clockTimezone/qrTimeoutSeconds (NOT document-root). Colors trust the
    // cloud's contrast guard but keep a defensive same-color check.
    cJSON *qshp = config ? cJSON_GetObjectItem(config, "qrShape") : NULL;
    cfg->qr_shape = qr_shape_of(cJSON_GetStringValue(qshp));
    cJSON *qfg = config ? cJSON_GetObjectItem(config, "qrFg") : NULL;
    cJSON *qbg = config ? cJSON_GetObjectItem(config, "qrBg") : NULL;
    cfg->qr_fg = parse_hex_color(cJSON_GetStringValue(qfg), 0x111111);
    cfg->qr_bg = parse_hex_color(cJSON_GetStringValue(qbg), 0xFFFFFF);
    if (cfg->qr_fg == cfg->qr_bg) {   // never render an invisible QR
        cfg->qr_fg = 0x111111;
        cfg->qr_bg = 0xFFFFFF;
    }

    // QR background plate: qrCornerRadius, sibling of qrShape/qrFg/qrBg
    // inside "config". Sanitizes independently and permissively — no
    // cross-field validation (unlike the fg/bg contrast guard above).
    cfg->qr_corner_radius = qr_corner_radius_of(config);

    // QR drop shadow: qrShadowMode/qrShadowStrength/qrShadowColor, siblings
    // of qrCorner. Older cloud sent a plain boolean `qrShadow` (no mode/
    // strength/color) — migrate qrShadow==true to DROP, but ONLY when
    // qrShadowMode itself is absent from the payload: a present-but-unknown
    // mode string is "unknown", not "absent", and stays NONE per spec (no
    // legacy fallback in that case).
    cJSON *qsmode = config ? cJSON_GetObjectItem(config, "qrShadowMode") : NULL;
    const char *qsmode_str = cJSON_GetStringValue(qsmode);
    if (qsmode_str) {
        cfg->qr_shadow_mode = qr_shadow_mode_of(qsmode_str);
    } else {
        cJSON *qshadow_legacy = config ? cJSON_GetObjectItem(config, "qrShadow") : NULL;
        cfg->qr_shadow_mode = (cJSON_IsBool(qshadow_legacy) && cJSON_IsTrue(qshadow_legacy))
                              ? QR_SHADOW_DROP : QR_SHADOW_NONE;
    }

    cJSON *qsstrength = config ? cJSON_GetObjectItem(config, "qrShadowStrength") : NULL;
    cfg->qr_shadow_strength = cJSON_IsNumber(qsstrength) ? (int)qsstrength->valuedouble : 50;
    if (cfg->qr_shadow_strength < 0)   cfg->qr_shadow_strength = 0;
    if (cfg->qr_shadow_strength > 100) cfg->qr_shadow_strength = 100;

    cJSON *qscolor = config ? cJSON_GetObjectItem(config, "qrShadowColor") : NULL;
    cfg->qr_shadow_color = parse_hex_color(cJSON_GetStringValue(qscolor), 0x000000);

    cJSON *dev = cJSON_GetObjectItem(root, "device");
    cJSON *br = dev ? cJSON_GetObjectItem(dev, "brightness") : NULL;
    cfg->device.brightness = cJSON_IsNumber(br) ? (int)br->valuedouble : 100;
    if (cfg->device.brightness < 10)  cfg->device.brightness = 10;
    if (cfg->device.brightness > 100) cfg->device.brightness = 100;

    cJSON *slp = dev ? cJSON_GetObjectItem(dev, "sleep") : NULL;
    cfg->device.sleep.enabled = slp ? cJSON_IsTrue(cJSON_GetObjectItem(slp, "enabled")) : false;
    cJSON *st = slp ? cJSON_GetObjectItem(slp, "timeoutSeconds") : NULL;
    cfg->device.sleep.timeout_seconds = cJSON_IsNumber(st) ? (int)st->valuedouble : 300;
    if (cfg->device.sleep.timeout_seconds < 30)   cfg->device.sleep.timeout_seconds = 30;
    if (cfg->device.sleep.timeout_seconds > 3600) cfg->device.sleep.timeout_seconds = 3600;

    copy_str(cfg->device.settings_password_hash, sizeof(cfg->device.settings_password_hash),
             dev ? cJSON_GetStringValue(cJSON_GetObjectItem(dev, "settingsPasswordHash")) : NULL);
    copy_str(cfg->device.settings_password_salt, sizeof(cfg->device.settings_password_salt),
             dev ? cJSON_GetStringValue(cJSON_GetObjectItem(dev, "settingsPasswordSalt")) : NULL);

    cJSON *mq = cJSON_GetObjectItem(root, "mqtt");
    if (mq) {
        copy_str(cfg->mqtt.host, sizeof(cfg->mqtt.host), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "host")));
        copy_str(cfg->mqtt.client_id, sizeof(cfg->mqtt.client_id), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "clientId")));
        copy_str(cfg->mqtt.username, sizeof(cfg->mqtt.username), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "username")));
        cJSON *port = cJSON_GetObjectItem(mq, "port");
        cfg->mqtt.port = cJSON_IsNumber(port) ? (int)port->valuedouble : 8883;
        cfg->mqtt.enabled = (cfg->mqtt.host[0] != '\0');
    }

    cJSON *pin = cJSON_GetObjectItem(root, "pin");
    if (cJSON_IsObject(pin)) {
        const char *purl = cJSON_GetStringValue(cJSON_GetObjectItem(pin, "url"));
        if (purl && purl[0] != '\0') {
            copy_str(cfg->pin_url, sizeof(cfg->pin_url), purl);
            cfg->pin_set = true;
        }
    }

    cJSON *screens = config ? cJSON_GetObjectItem(config, "screens") : NULL;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        cJSON *scr = screens ? cJSON_GetObjectItem(screens, SCREEN_NAMES[i]) : NULL;
        cJSON *objs = scr ? cJSON_GetObjectItem(scr, "objects") : NULL;
        parse_screen(objs, &cfg->screens[i]);

        cJSON *cols = scr ? cJSON_GetObjectItem(scr, "colors") : NULL;
        cfg_screen_t *cs = &cfg->screens[i];
        cs->has_colors =
            cJSON_IsObject(cols) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "bg")),     &cs->col_bg) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "fg")),     &cs->col_fg) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "accent")), &cs->col_accent) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "muted")),  &cs->col_muted);
    }

    cfg->valid = true;
    cJSON_Delete(root);
    return true;
}
