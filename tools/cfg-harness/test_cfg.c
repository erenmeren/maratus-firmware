#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "cfg_parse.h"
#include "render_geom.h"
#include "qr_style.h"
#include "asset_key.h"
#include "asset_evict.h"
#include "clock_format.h"
#include "provisioning.h"
#include "wifi_util.h"
#include "ota_manifest.h"
#include "sleep_policy.h"
#include "settings_pin.h"
#include "sha256.h"
#include "gesture.h"
#include "overrides.h"
#include "brightness.h"
#include "command_parse.h"
#include "mqtt_topics.h"
#include "cmd_envelope.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f && "fixture not found");
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    assert(b && "malloc failed");
    size_t rd = fread(b, 1, (size_t)n, f);
    assert(rd == (size_t)n && "fread short");
    b[n] = '\0';
    fclose(f);
    return b;
}

static void test_parse(void) {
    char *json = slurp("fixtures/sample-config.json");
    device_config_t cfg;
    bool ok = cfg_parse_json(json, &cfg);
    assert(ok && cfg.valid);

    // Brand tokens (brandWarn absent -> default 0xF59E0B).
    assert(cfg.brand_bg == 0x0B5D3B);
    assert(cfg.brand_fg == 0xFFFFFF);
    assert(cfg.brand_warn == 0xF59E0B);
    assert(strcmp(cfg.etag, "abc123") == 0);
    assert(strcmp(cfg.clock_timezone, "America/New_York") == 0);
    assert(cfg.clock_24h == false);
    assert(cfg.wifi_level == 3);
    assert(strstr(cfg.logo_url, "/branding/org_1/logo.png") != NULL);

    // idle: hidden object dropped -> 1 text + 1 image visible.
    cfg_screen_t *idle = &cfg.screens[SCREEN_IDLE];
    assert(idle->n == 2);
    assert(idle->objects[0].type == OBJ_TEXT);
    assert(strcmp(idle->objects[0].text, "Welcome") == 0);
    assert(idle->objects[1].type == OBJ_IMAGE);
    assert(strstr(idle->objects[1].image_url, "/branding/org_1/images/x.png") != NULL);
    // top-level wordmark parsed.
    assert(strcmp(cfg.wordmark, "Roastwell Coffee") == 0);

    // qr: a qr widget + a text.
    cfg_screen_t *qr = &cfg.screens[SCREEN_QR];
    assert(qr->n == 2);

    // sent: clock widget with no align -> defaults to center (matches branding preview).
    cfg_screen_t *sent = &cfg.screens[SCREEN_SENT];
    assert(sent->n == 1);
    assert(sent->objects[0].type == OBJ_CLOCK);
    assert(sent->objects[0].align == CFG_ALIGN_CENTER);

    // setup: pairingCode widget parsed.
    assert(cfg.screens[SCREEN_SETUP].objects[0].type == OBJ_PAIRING_CODE);

    // M7 device policies: brightness clamps to 100, timeout clamps down to 30.
    assert(cfg.device.brightness == 100);
    assert(cfg.device.sleep.enabled == true);
    assert(cfg.device.sleep.timeout_seconds == 30);
    assert(strcmp(cfg.device.settings_password_hash,
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    assert(strcmp(cfg.device.settings_password_salt, "abc123") == 0);

    // (f) existing full fixture has no top-level "pin" -> pin_set stays false.
    assert(cfg.pin_set == false);

    // (g) fixture predates the "pinned" screen key -> parses to an empty
    // screen (old cached configs), not a crash/garbage.
    assert(cfg.screens[SCREEN_PINNED].n == 0);

    // (h) fixture predates qrShape/qrFg/qrBg -> old cloud (fields absent)
    // renders identically to today: rounded, near-black on white.
    assert(cfg.qr_shape == QR_SHAPE_ROUNDED);
    assert(cfg.qr_fg == 0x111111);
    assert(cfg.qr_bg == 0xFFFFFF);

    // (i) fixture also predates qrCornerRadius/qrShadow -> defaults (24 =
    // today's live look, no shadow).
    assert(cfg.qr_corner_radius == 24);
    assert(cfg.qr_shadow_mode == QR_SHADOW_NONE);

    // (j) fixture predates qrShadowMode/qrShadowStrength/qrShadowColor ->
    // defaults (none/50/#000000).
    assert(cfg.qr_shadow_strength == 50);
    assert(cfg.qr_shadow_color == 0x000000);

    free(json);
    printf("test_parse OK\n");
}

static void test_geom(void) {
    assert(geom_frac_to_px(0.0f) == 0);
    assert(geom_frac_to_px(1.0f) == 720);
    assert(geom_frac_to_px(0.5f) == 360);
    assert(geom_frac_to_px(-1.0f) == 0);     // clamp low
    assert(geom_frac_to_px(2.0f) == 720);    // clamp high

    cfg_object_t o = { .x = 0.1f, .y = 0.2f, .w = 0.8f, .h = 0.15f };
    px_box_t b = geom_box(&o);
    assert(b.x == 72 && b.y == 144 && b.w == 576 && b.h == 108);
    printf("test_geom OK\n");
}

static void test_qr_style(void) {
    // module_px: fits (modules + 2*quiet) cells inside the box side, floors.
    // 21-module (version-1) QR, quiet=4 -> 29 cells. 720 / 29 = 24 (floor).
    assert(qr_module_px(720, 21, 4) == 24);
    // Exact division.
    assert(qr_module_px(290, 21, 4) == 10);
    // Box smaller than modules -> floors to a minimum of 1, never 0 (would
    // divide-by-zero / infinite-loop callers that step by module_px).
    assert(qr_module_px(10, 21, 4) == 1);
    // Degenerate modules count -> 0 (nothing to draw), no crash.
    assert(qr_module_px(720, 0, 4) == 0);

    // canvas_px: module_px * total cells.
    assert(qr_canvas_px(24, 21, 4) == 24 * 29);
    assert(qr_canvas_px(10, 21, 4) == 290);

    // dot diameter: ~0.85x module_px, clamped to [1, module_px].
    assert(qr_dot_diameter_px(24) == 20);   // round(24*0.85) = 20.4 -> 20
    assert(qr_dot_diameter_px(10) == 9);    // round(8.5) -> 9 (round-half-up)
    assert(qr_dot_diameter_px(1)  == 1);    // never vanishes
    assert(qr_dot_diameter_px(0)  == 1);    // clamped up even for a degenerate cell

    // finder radius: ~1/3 of the shape's pixel size, clamped >= 1.
    assert(qr_finder_radius_px(7 * 24) == 56);   // 168/3 = 56
    assert(qr_finder_radius_px(3 * 24) == 24);   // 72/3 = 24
    assert(qr_finder_radius_px(1) == 1);         // never 0

    // finder-cell test: three 7x7 blocks at fixed corners of a 21-module QR.
    // Top-left block.
    assert(qr_in_finder_block(21, 0, 0));
    assert(qr_in_finder_block(21, 6, 6));
    assert(!qr_in_finder_block(21, 7, 0));    // just outside the TL block
    assert(!qr_in_finder_block(21, 0, 7));
    // Top-right block (modules-7 .. modules-1).
    assert(qr_in_finder_block(21, 14, 0));
    assert(qr_in_finder_block(21, 20, 6));
    assert(!qr_in_finder_block(21, 13, 0));
    // Bottom-left block.
    assert(qr_in_finder_block(21, 0, 14));
    assert(qr_in_finder_block(21, 6, 20));
    assert(!qr_in_finder_block(21, 0, 13));
    // Center of the symbol and the bottom-right corner (no finder there —
    // the 4th corner deliberately has none) are not finder cells.
    assert(!qr_in_finder_block(21, 10, 10));
    assert(!qr_in_finder_block(21, 20, 20));
    // Degenerate/too-small module count -> no finder blocks at all.
    assert(!qr_in_finder_block(6, 0, 0));

    // dots diameter: ~0.7x module_px, clamped [1, module_px].
    assert(qr_dots_diameter_px(24) == 17);   // round(16.8) -> 17
    assert(qr_dots_diameter_px(10) == 7);    // round(7.0) -> 7
    assert(qr_dots_diameter_px(1)  == 1);
    assert(qr_dots_diameter_px(0)  == 1);

    // soft corner radius: ~0.25x module_px, clamped [1, module_px/2].
    assert(qr_soft_radius_px(24) == 6);      // round(6.0) -> 6
    assert(qr_soft_radius_px(10) == 3);      // round(2.5) -> 3 (half-up)
    assert(qr_soft_radius_px(2)  == 1);      // clamp to max_r=1
    assert(qr_soft_radius_px(0)  == 1);      // never 0

    // finder radius per shape: classic -> square (0), others -> existing radius.
    assert(qr_finder_radius_px_for(QR_SHAPE_CLASSIC, 7 * 24) == 0);
    assert(qr_finder_radius_px_for(QR_SHAPE_SOFT,    7 * 24) == qr_finder_radius_px(7 * 24));
    assert(qr_finder_radius_px_for(QR_SHAPE_ROUNDED, 7 * 24) == qr_finder_radius_px(7 * 24));
    assert(qr_finder_radius_px_for(QR_SHAPE_DOTS,    7 * 24) == qr_finder_radius_px(7 * 24));

    // per-shape module draw params.
    qr_module_style_t classic = qr_module_style(QR_SHAPE_CLASSIC, 24);
    assert(classic.kind == QR_DRAW_SQUARE);
    assert(classic.size == 24);
    assert(classic.offset == 0);

    qr_module_style_t soft = qr_module_style(QR_SHAPE_SOFT, 24);
    assert(soft.kind == QR_DRAW_ROUNDED_SQUARE);
    assert(soft.size == 24);
    assert(soft.offset == 0);
    assert(soft.radius == qr_soft_radius_px(24));

    qr_module_style_t rounded = qr_module_style(QR_SHAPE_ROUNDED, 24);
    assert(rounded.kind == QR_DRAW_CIRCLE);
    assert(rounded.size == qr_dot_diameter_px(24));
    assert(rounded.offset == (24 - rounded.size) / 2);

    qr_module_style_t dots = qr_module_style(QR_SHAPE_DOTS, 24);
    assert(dots.kind == QR_DRAW_CIRCLE);
    assert(dots.size == qr_dots_diameter_px(24));
    assert(dots.offset == (24 - dots.size) / 2);
    assert(dots.size < rounded.size);   // dots (0.7x) tighter than rounded (0.85x)

    // bg-plate corner radius: round(dim * 0.30 * v/100) — CLOUD PARITY with
    // lib/qr-svg.ts qrCornerRadiusPx.
    assert(qr_corner_radius_px(260, 24)  == 19);   // round(18.72) -> 19 (spec example)
    assert(qr_corner_radius_px(400, 100) == 120);  // round(120.0) -> 120
    assert(qr_corner_radius_px(400, 0)   == 0);    // v=0 -> always square
    assert(qr_corner_radius_px(0,   24)  == 0);    // degenerate canvas -> 0, no crash
    assert(qr_corner_radius_px(-5,  24)  == 0);
    assert(qr_corner_radius_px(400, -10) == 0);    // v clamps low
    assert(qr_corner_radius_px(400, 250) == qr_corner_radius_px(400, 100));   // v clamps high

    // shadow params: CLOUD PARITY with lib/qr-svg.ts qrShadowParams. Spec
    // example: dim=260, s=0.5 (strength=50) -> blur~=36px, drop opa~=128
    // (alpha 0.5 * 255), neon spread~=73px.
    qr_shadow_params_t none = qr_shadow_params(QR_SHADOW_NONE, 50, 260);
    assert(none.blur_px == 0 && none.offset_y_px == 0 && none.opa == 0 && none.spread_px == 0);

    qr_shadow_params_t drop50 = qr_shadow_params(QR_SHADOW_DROP, 50, 260);
    assert(drop50.blur_px == 36);          // round(260*0.14) = round(36.4) -> 36
    assert(drop50.offset_y_px == 5);       // round(260*0.02) = round(5.2) -> 5
    assert(drop50.opa == 128);             // round(0.5*255) = round(127.5) -> 128
    assert(drop50.spread_px == 0);         // drop is a single pass

    qr_shadow_params_t neon50 = qr_shadow_params(QR_SHADOW_NEON, 50, 260);
    assert(neon50.blur_px == 36);          // same blur curve as drop
    assert(neon50.offset_y_px == 0);       // glow surrounds evenly, no "down"
    assert(neon50.opa == 255);             // full-color halo, mode-independent of strength
    assert(neon50.spread_px == 73);        // round(2 * 36.4) = round(72.8) -> 73 (from the
                                            // UNROUNDED blur, not 2*blur_px==72)

    // strength endpoints (drop): alpha 0.25 .. 0.75 -> opa 64 .. 191.
    assert(qr_shadow_params(QR_SHADOW_DROP, 0,   260).opa == 64);    // round(0.25*255)=round(63.75)->64
    assert(qr_shadow_params(QR_SHADOW_DROP, 100, 260).opa == 191);   // round(0.75*255)=round(191.25)->191
    // strength endpoints (neon): opa always 255 regardless of strength.
    assert(qr_shadow_params(QR_SHADOW_NEON, 0,   260).opa == 255);
    assert(qr_shadow_params(QR_SHADOW_NEON, 100, 260).opa == 255);
    // blur scales with dim (dim-relative, not a fixed px constant) — the
    // whole point of the parity fix.
    assert(qr_shadow_params(QR_SHADOW_DROP, 50, 130).blur_px == 18);   // half of dim=260's 36
    assert(qr_shadow_params(QR_SHADOW_DROP, 50, 520).blur_px == 73);   // double of dim=260's 36(.4)

    // out-of-range strength clamps rather than crashing/underflowing.
    assert(qr_shadow_params(QR_SHADOW_DROP, -50, 260).opa == 64);
    assert(qr_shadow_params(QR_SHADOW_NEON, 999, 260).opa == 255);
    // degenerate/negative dim -> zeroed struct, no crash.
    qr_shadow_params_t degenerate = qr_shadow_params(QR_SHADOW_DROP, 50, 0);
    assert(degenerate.blur_px == 0 && degenerate.opa == 0);
    qr_shadow_params_t neg_dim = qr_shadow_params(QR_SHADOW_DROP, 50, -10);
    assert(neg_dim.blur_px == 0 && neg_dim.opa == 0);

    printf("test_qr_style OK\n");
}

static void test_zsort(void) {
    cfg_object_t objs[3];
    memset(objs, 0, sizeof(objs));
    objs[0].z = 5; objs[1].z = 1; objs[2].z = 5;   // ties keep input order
    int order[3];
    cfg_sort_by_z(objs, 3, order);
    assert(order[0] == 1);   // z=1 first
    assert(order[1] == 0);   // z=5, input index 0 before...
    assert(order[2] == 2);   // ...z=5, input index 2 (stable)
    printf("test_zsort OK\n");
}

static void test_map(void) {
    assert(screen_for_state(DEV_IDLE)       == SCREEN_IDLE);
    assert(screen_for_state(DEV_PROCESSING) == SCREEN_PROCESSING);
    assert(screen_for_state(DEV_UPLOADING)  == SCREEN_PROCESSING);   // shares processing
    assert(screen_for_state(DEV_QR)         == SCREEN_QR);
    assert(screen_for_state(DEV_SENT)       == SCREEN_SENT);
    assert(screen_for_state(DEV_ERROR)      == SCREEN_ERROR);
    assert(screen_for_state(DEV_PAUSED)     == SCREEN_PAUSED);
    assert(screen_for_state(DEV_SETUP)      == SCREEN_SETUP);
    assert(screen_for_state(DEV_BOOT)       == SCREEN_IDLE);         // boot fallback
    assert(screen_for_state(DEV_PINNED)     == SCREEN_QR);           // cfg-unaware fallback
    printf("test_map OK\n");
}

static void test_screen_for_pinned(void) {
    // No cfg / not-yet-valid cfg -> safe SCREEN_QR fallback.
    assert(screen_for_pinned(NULL) == SCREEN_QR);
    device_config_t invalid; memset(&invalid, 0, sizeof(invalid));
    invalid.valid = false;
    assert(screen_for_pinned(&invalid) == SCREEN_QR);

    // Valid cfg, pinned screen empty (old cached config with no "pinned" key,
    // or a tenant who never edited it) -> SCREEN_QR fallback.
    device_config_t empty; memset(&empty, 0, sizeof(empty));
    empty.valid = true;
    assert(screen_for_pinned(&empty) == SCREEN_QR);

    // Valid cfg, pinned screen has >=1 object -> the dedicated SCREEN_PINNED layout.
    device_config_t populated; memset(&populated, 0, sizeof(populated));
    populated.valid = true;
    populated.screens[SCREEN_PINNED].n = 1;
    populated.screens[SCREEN_PINNED].objects[0].type = OBJ_QR;
    assert(screen_for_pinned(&populated) == SCREEN_PINNED);

    printf("test_screen_for_pinned OK\n");
}

static void test_pinned_screen_parse(void) {
    // Pinned screen with objects parses like any other screen.
    const char *j1 =
        "{\"version\":\"1\",\"config\":{\"screens\":{\"pinned\":{\"objects\":["
        "{\"type\":\"qr\",\"x\":0.25,\"y\":0.25,\"w\":0.5,\"h\":0.5,\"visible\":true,\"z\":1},"
        "{\"type\":\"text\",\"x\":0.1,\"y\":0.05,\"w\":0.8,\"h\":0.1,\"visible\":true,\"z\":2,"
        "\"text\":\"Menu\",\"fontSize\":32,\"align\":\"center\"}"
        "]}}}}";
    device_config_t c1; assert(cfg_parse_json(j1, &c1));
    assert(c1.screens[SCREEN_PINNED].n == 2);
    assert(c1.screens[SCREEN_PINNED].objects[0].type == OBJ_QR);
    assert(c1.screens[SCREEN_PINNED].objects[1].type == OBJ_TEXT);
    assert(strcmp(c1.screens[SCREEN_PINNED].objects[1].text, "Menu") == 0);
    assert(screen_for_pinned(&c1) == SCREEN_PINNED);

    // Pinned screen present but with an empty objects array -> falls back.
    const char *j2 = "{\"version\":\"1\",\"config\":{\"screens\":{\"pinned\":{\"objects\":[]}}}}";
    device_config_t c2; assert(cfg_parse_json(j2, &c2));
    assert(c2.screens[SCREEN_PINNED].n == 0);
    assert(screen_for_pinned(&c2) == SCREEN_QR);

    // "pinned" key absent entirely (old cloud) -> also falls back.
    const char *j3 = "{\"version\":\"1\",\"config\":{\"screens\":{}}}";
    device_config_t c3; assert(cfg_parse_json(j3, &c3));
    assert(c3.screens[SCREEN_PINNED].n == 0);
    assert(screen_for_pinned(&c3) == SCREEN_QR);

    printf("test_pinned_screen_parse OK\n");
}

static void test_home_state(void) {
    // Pure helper: which lifecycle state "go home" lands on, given a pin.
    assert(home_state(true)  == DEV_PINNED);
    assert(home_state(false) == DEV_IDLE);
    printf("test_home_state OK\n");
}

static void test_asset_key(void) {
    // Presigned R2 URLs for the same object differ ONLY in the query string
    // (X-Amz-*). Stripping the query yields an identical, stable key.
    char k1[41], k2[41];
    asset_key("https://r2.example.com/branding/org_1/logo.png"
              "?X-Amz-Signature=aaaa&X-Amz-Date=20260616T000000Z", k1);
    asset_key("https://r2.example.com/branding/org_1/logo.png"
              "?X-Amz-Signature=bbbb&X-Amz-Date=20260617T120000Z", k2);
    assert(strcmp(k1, k2) == 0 && "same object -> same key regardless of query");
    assert(strlen(k1) == 40 && "key is exactly 40 hex chars");

    // A bare URL (no query) hashes the same as the same URL WITH a query.
    char k3[41];
    asset_key("https://r2.example.com/branding/org_1/logo.png", k3);
    assert(strcmp(k1, k3) == 0 && "no-query URL matches query-stripped key");

    // Different paths -> different keys.
    char ka[41], kb[41];
    asset_key("https://x.test/a", ka);
    asset_key("https://x.test/b", kb);
    assert(strcmp(ka, kb) != 0 && "different paths -> different keys");

    // Known-answer: sha1("https://x.test/a") computed independently.
    assert(strcmp(ka, "264344770ca8cf985c7e0b079449f649f7c8ba9c") == 0
           && "stable SHA-1 known-answer");

    // Output is lowercase hex, exactly 40 chars + NUL.
    assert(strlen(ka) == 40);
    for (int i = 0; i < 40; i++) {
        char c = ka[i];
        assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    assert(ka[40] == '\0');

    // A query-only-differing URL where the query contains a '?'-like '/' path
    // is not confused: only the FIRST '?' splits.
    char kq1[41], kq2[41];
    asset_key("https://x.test/a?foo=1", kq1);
    asset_key("https://x.test/a?bar=2&baz=3", kq2);
    assert(strcmp(kq1, kq2) == 0 && strcmp(kq1, ka) == 0);

    // NULL and "" -> empty out, no crash.
    char kn[41]; kn[0] = 'Z';
    asset_key(NULL, kn);
    assert(kn[0] == '\0' && "NULL url -> empty key");
    char ke[41]; ke[0] = 'Z';
    asset_key("", ke);
    assert(ke[0] == '\0' && "empty url -> empty key");

    printf("test_asset_key OK\n");
}

static void test_asset_evict(void) {
    // on_flash that are NOT referenced should be returned for deletion.
    const char *flash[] = { "aaa", "bbb", "ccc", "ddd" };
    const char *ref[]   = { "bbb", "ddd" };
    const char *out[8];

    int n = asset_evict_list(flash, 4, ref, 2, out, 8);
    assert(n == 2 && "two unreferenced keys");
    // Order preserved from on_flash: aaa then ccc.
    assert(strcmp(out[0], "aaa") == 0);
    assert(strcmp(out[1], "ccc") == 0);
    // Returned pointers come from on_flash array.
    assert(out[0] == flash[0] && out[1] == flash[2]);

    // Empty referenced -> everything is evicted.
    int all = asset_evict_list(flash, 4, NULL, 0, out, 8);
    assert(all == 4);

    // All referenced -> nothing evicted.
    const char *refall[] = { "aaa", "bbb", "ccc", "ddd" };
    int none = asset_evict_list(flash, 4, refall, 4, out, 8);
    assert(none == 0);

    // Duplicate on_flash keys: each unreferenced occurrence reported once it is
    // not in referenced; a referenced dup is kept. Dedup of referenced handled.
    const char *dupflash[] = { "x", "x", "y" };
    const char *dupref[]   = { "y", "y" };   // duplicate reference deduped sensibly
    int dn = asset_evict_list(dupflash, 3, dupref, 2, out, 8);
    assert(dn == 2 && "both 'x' entries evicted, 'y' kept");
    assert(strcmp(out[0], "x") == 0 && strcmp(out[1], "x") == 0);

    // max_out clamp respected.
    int clamped = asset_evict_list(flash, 4, NULL, 0, out, 2);
    assert(clamped == 2 && "clamped to max_out");

    // Zero-size flash -> nothing.
    assert(asset_evict_list(NULL, 0, ref, 2, out, 8) == 0);

    printf("test_asset_evict OK\n");
}

static void test_clock(void) {
    struct tm t = {0};
    t.tm_hour = 14; t.tm_min = 5; t.tm_wday = 3; t.tm_mday = 17; t.tm_mon = 5; // Wed 17 Jun
    char buf[64];

    // --- time line ---
    format_clock_time(&t, true, buf, sizeof(buf));
    assert(strcmp(buf, "14:05") == 0);

    format_clock_time(&t, false, buf, sizeof(buf));
    assert(strcmp(buf, "2:05 PM") == 0);

    struct tm noon = {0}; noon.tm_hour = 12; noon.tm_min = 0;
    format_clock_time(&noon, false, buf, sizeof(buf));
    assert(strcmp(buf, "12:00 PM") == 0);

    struct tm midnight = {0}; midnight.tm_hour = 0; midnight.tm_min = 0;
    format_clock_time(&midnight, false, buf, sizeof(buf));
    assert(strcmp(buf, "12:00 AM") == 0);

    // --- date line (matches branding preview: full weekday + full month) ---
    format_clock_date(&t, true, buf, sizeof(buf));
    assert(strcmp(buf, "Wednesday, June 17") == 0);

    format_clock_date(&t, false, buf, sizeof(buf));
    assert(strcmp(buf, "June 17") == 0);

    printf("test_clock OK\n");
}

static void test_mmss(void) {
    char buf[16];
    format_mmss(0, buf, sizeof(buf));   assert(strcmp(buf, "0:00") == 0);
    format_mmss(48, buf, sizeof(buf));  assert(strcmp(buf, "0:48") == 0);
    format_mmss(125, buf, sizeof(buf)); assert(strcmp(buf, "2:05") == 0);
    format_mmss(600, buf, sizeof(buf)); assert(strcmp(buf, "10:00") == 0);
    format_mmss(-5, buf, sizeof(buf));  assert(strcmp(buf, "0:00") == 0);
    printf("test_mmss OK\n");
}

static void test_qr_timeout(void) {
    device_config_t cfg;

    // present value: 90 -> 90
    const char *j1 = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":90}}";
    assert(cfg_parse_json(j1, &cfg) && cfg.qr_timeout_seconds == 90);

    // absent key -> default 60
    const char *j2 = "{\"version\":\"v1\",\"config\":{}}";
    assert(cfg_parse_json(j2, &cfg) && cfg.qr_timeout_seconds == 60);

    // at min (5) -> kept
    const char *j3 = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":5}}";
    assert(cfg_parse_json(j3, &cfg) && cfg.qr_timeout_seconds == 5);

    // below min (1) -> clamped to 5
    const char *j3b = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":1}}";
    assert(cfg_parse_json(j3b, &cfg) && cfg.qr_timeout_seconds == 5);

    // above max (999) -> clamped to 180
    const char *j4 = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":999}}";
    assert(cfg_parse_json(j4, &cfg) && cfg.qr_timeout_seconds == 180);

    printf("test_qr_timeout OK\n");
}

static void test_qr_shape_colors(void) {
    device_config_t cfg;

    // absent -> defaults (rounded, near-black on white).
    const char *j1 = "{\"version\":\"v1\",\"config\":{}}";
    assert(cfg_parse_json(j1, &cfg));
    assert(cfg.qr_shape == QR_SHAPE_ROUNDED);
    assert(cfg.qr_fg == 0x111111);
    assert(cfg.qr_bg == 0xFFFFFF);

    // each recognized shape string.
    const char *j2 = "{\"version\":\"v1\",\"config\":{\"qrShape\":\"classic\"}}";
    assert(cfg_parse_json(j2, &cfg) && cfg.qr_shape == QR_SHAPE_CLASSIC);
    const char *j3 = "{\"version\":\"v1\",\"config\":{\"qrShape\":\"soft\"}}";
    assert(cfg_parse_json(j3, &cfg) && cfg.qr_shape == QR_SHAPE_SOFT);
    const char *j4 = "{\"version\":\"v1\",\"config\":{\"qrShape\":\"rounded\"}}";
    assert(cfg_parse_json(j4, &cfg) && cfg.qr_shape == QR_SHAPE_ROUNDED);
    const char *j5 = "{\"version\":\"v1\",\"config\":{\"qrShape\":\"dots\"}}";
    assert(cfg_parse_json(j5, &cfg) && cfg.qr_shape == QR_SHAPE_DOTS);

    // unknown shape string -> default rounded.
    const char *j6 = "{\"version\":\"v1\",\"config\":{\"qrShape\":\"triangle\"}}";
    assert(cfg_parse_json(j6, &cfg) && cfg.qr_shape == QR_SHAPE_ROUNDED);

    // valid distinct colors -> parsed as given.
    const char *j7 =
        "{\"version\":\"v1\",\"config\":{\"qrFg\":\"#ff0000\",\"qrBg\":\"#00ff00\"}}";
    assert(cfg_parse_json(j7, &cfg));
    assert(cfg.qr_fg == 0xFF0000);
    assert(cfg.qr_bg == 0x00FF00);

    // malformed hex (either field) -> that field's default.
    const char *j8 =
        "{\"version\":\"v1\",\"config\":{\"qrFg\":\"nope\",\"qrBg\":\"#00ff00\"}}";
    assert(cfg_parse_json(j8, &cfg));
    assert(cfg.qr_fg == 0x111111);
    assert(cfg.qr_bg == 0x00FF00);

    const char *j9 =
        "{\"version\":\"v1\",\"config\":{\"qrFg\":\"#ff0000\",\"qrBg\":\"nope\"}}";
    assert(cfg_parse_json(j9, &cfg));
    assert(cfg.qr_fg == 0xFF0000);
    assert(cfg.qr_bg == 0xFFFFFF);

    // defensive: fg==bg (even if the cloud's contrast guard should have caught
    // it) -> both reset to defaults, never an invisible QR.
    const char *j10 =
        "{\"version\":\"v1\",\"config\":{\"qrFg\":\"#ff0000\",\"qrBg\":\"#ff0000\"}}";
    assert(cfg_parse_json(j10, &cfg));
    assert(cfg.qr_fg == 0x111111);
    assert(cfg.qr_bg == 0xFFFFFF);

    printf("test_qr_shape_colors OK\n");
}

// config.qrCornerRadius: 0..100 int slider, replacing the earlier 2-value
// `qrCorner: "square" | "rounded"` enum (2026-07-24). Mirrors the cloud's
// sanitizeQrCornerRadius (ditto-admin lib/printer-layout.ts): a valid stored
// number always wins (clamped+rounded); otherwise a legacy `qrCorner` string
// maps 1:1 (rounded -> 24, square -> 0); anything else -> the default (24).
static void test_qr_corner_shadow(void) {
    device_config_t cfg;

    // absent -> default 24 (today's live look) — matches the cloud's
    // DEFAULT_QR_STYLE.qrCornerRadius.
    const char *j1 = "{\"version\":\"v1\",\"config\":{}}";
    assert(cfg_parse_json(j1, &cfg));
    assert(cfg.qr_corner_radius == 24);

    // explicit qrCornerRadius value passes through as-is.
    const char *j2 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":60}}";
    assert(cfg_parse_json(j2, &cfg));
    assert(cfg.qr_corner_radius == 60);

    // 0 (square) is a valid, distinct value from "absent".
    const char *j2b = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":0}}";
    assert(cfg_parse_json(j2b, &cfg));
    assert(cfg.qr_corner_radius == 0);

    // below-range clamps to 0.
    const char *j3 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":-40}}";
    assert(cfg_parse_json(j3, &cfg));
    assert(cfg.qr_corner_radius == 0);

    // above-range clamps to 100.
    const char *j4 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":250}}";
    assert(cfg_parse_json(j4, &cfg));
    assert(cfg.qr_corner_radius == 100);

    // fractional value rounds half-up.
    const char *j5 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":24.5}}";
    assert(cfg_parse_json(j5, &cfg));
    assert(cfg.qr_corner_radius == 25);

    // non-number qrCornerRadius -> not "present", falls through to legacy/default.
    const char *j6 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":\"lots\"}}";
    assert(cfg_parse_json(j6, &cfg));
    assert(cfg.qr_corner_radius == 24);

    // legacy fallback: qrCornerRadius absent, legacy qrCorner "square" -> 0.
    const char *j7 = "{\"version\":\"v1\",\"config\":{\"qrCorner\":\"square\"}}";
    assert(cfg_parse_json(j7, &cfg));
    assert(cfg.qr_corner_radius == 0);

    // legacy fallback: qrCornerRadius absent, legacy qrCorner "rounded" -> 24.
    const char *j8 = "{\"version\":\"v1\",\"config\":{\"qrCorner\":\"rounded\"}}";
    assert(cfg_parse_json(j8, &cfg));
    assert(cfg.qr_corner_radius == 24);

    // legacy fallback: unknown qrCorner string -> default 24.
    const char *j9 = "{\"version\":\"v1\",\"config\":{\"qrCorner\":\"beveled\"}}";
    assert(cfg_parse_json(j9, &cfg));
    assert(cfg.qr_corner_radius == 24);

    // modern qrCornerRadius wins over a legacy qrCorner when both are present.
    const char *j10 = "{\"version\":\"v1\",\"config\":{\"qrCornerRadius\":10,\"qrCorner\":\"square\"}}";
    assert(cfg_parse_json(j10, &cfg));
    assert(cfg.qr_corner_radius == 10);

    printf("test_qr_corner_shadow OK\n");
}

// config.qrShadowMode/qrShadowStrength/qrShadowColor: siblings of qrCorner.
// Covers the new-cloud contract, the legacy-bool migration, and clamping.
static void test_qr_shadow_fields(void) {
    device_config_t cfg;

    // (1) everything absent -> defaults: none/50/#000000.
    const char *j1 = "{\"version\":\"v1\",\"config\":{}}";
    assert(cfg_parse_json(j1, &cfg));
    assert(cfg.qr_shadow_mode == QR_SHADOW_NONE);
    assert(cfg.qr_shadow_strength == 50);
    assert(cfg.qr_shadow_color == 0x000000);

    // (2) explicit qrShadowMode values pass through.
    const char *j2 = "{\"version\":\"v1\",\"config\":{\"qrShadowMode\":\"drop\"}}";
    assert(cfg_parse_json(j2, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_DROP);
    const char *j3 = "{\"version\":\"v1\",\"config\":{\"qrShadowMode\":\"neon\"}}";
    assert(cfg_parse_json(j3, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NEON);
    const char *j4 = "{\"version\":\"v1\",\"config\":{\"qrShadowMode\":\"none\"}}";
    assert(cfg_parse_json(j4, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NONE);

    // (3) unknown qrShadowMode string -> NONE. Present-but-unknown is NOT
    // "absent", so no legacy fallback even if a legacy qrShadow is present too.
    const char *j5 =
        "{\"version\":\"v1\",\"config\":{\"qrShadowMode\":\"sparkle\",\"qrShadow\":true}}";
    assert(cfg_parse_json(j5, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NONE);

    // (4) legacy migration: qrShadowMode absent + legacy qrShadow:true -> DROP.
    const char *j6 = "{\"version\":\"v1\",\"config\":{\"qrShadow\":true}}";
    assert(cfg_parse_json(j6, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_DROP);

    // (5) legacy migration: qrShadowMode absent + legacy qrShadow:false -> NONE.
    const char *j7 = "{\"version\":\"v1\",\"config\":{\"qrShadow\":false}}";
    assert(cfg_parse_json(j7, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NONE);

    // (6) legacy migration: non-bool qrShadow -> NONE (never crash on a wrong
    // JSON type).
    const char *j8 = "{\"version\":\"v1\",\"config\":{\"qrShadow\":\"yes\"}}";
    assert(cfg_parse_json(j8, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NONE);
    const char *j9 = "{\"version\":\"v1\",\"config\":{\"qrShadow\":1}}";
    assert(cfg_parse_json(j9, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NONE);

    // (7) modern qrShadowMode present wins over a legacy qrShadow, in both
    // directions (mode says NEON even though legacy bool says false/absent-ish).
    const char *j10 =
        "{\"version\":\"v1\",\"config\":{\"qrShadowMode\":\"neon\",\"qrShadow\":false}}";
    assert(cfg_parse_json(j10, &cfg) && cfg.qr_shadow_mode == QR_SHADOW_NEON);

    // (8) qrShadowStrength: valid int passes through.
    const char *j11 = "{\"version\":\"v1\",\"config\":{\"qrShadowStrength\":30}}";
    assert(cfg_parse_json(j11, &cfg) && cfg.qr_shadow_strength == 30);

    // (9) qrShadowStrength: absent -> default 50.
    assert(cfg_parse_json(j1, &cfg) && cfg.qr_shadow_strength == 50);

    // (10) qrShadowStrength: below min clamps to 0.
    const char *j12 = "{\"version\":\"v1\",\"config\":{\"qrShadowStrength\":-20}}";
    assert(cfg_parse_json(j12, &cfg) && cfg.qr_shadow_strength == 0);

    // (11) qrShadowStrength: above max clamps to 100.
    const char *j13 = "{\"version\":\"v1\",\"config\":{\"qrShadowStrength\":250}}";
    assert(cfg_parse_json(j13, &cfg) && cfg.qr_shadow_strength == 100);

    // (12) qrShadowStrength: non-number -> default 50 (never crash on a
    // wrong JSON type).
    const char *j14 = "{\"version\":\"v1\",\"config\":{\"qrShadowStrength\":\"lots\"}}";
    assert(cfg_parse_json(j14, &cfg) && cfg.qr_shadow_strength == 50);

    // (13) qrShadowColor: valid hex (with and without '#') parses.
    const char *j15 = "{\"version\":\"v1\",\"config\":{\"qrShadowColor\":\"#ff00ff\"}}";
    assert(cfg_parse_json(j15, &cfg) && cfg.qr_shadow_color == 0xFF00FF);
    const char *j16 = "{\"version\":\"v1\",\"config\":{\"qrShadowColor\":\"00aaff\"}}";
    assert(cfg_parse_json(j16, &cfg) && cfg.qr_shadow_color == 0x00AAFF);

    // (14) qrShadowColor: malformed -> default #000000.
    const char *j17 = "{\"version\":\"v1\",\"config\":{\"qrShadowColor\":\"nope\"}}";
    assert(cfg_parse_json(j17, &cfg) && cfg.qr_shadow_color == 0x000000);

    // (15) qrShadowColor: absent -> default #000000.
    assert(cfg_parse_json(j1, &cfg) && cfg.qr_shadow_color == 0x000000);

    printf("test_qr_shadow_fields OK\n");
}

// NEON pre-rendered canvas glow (fw 0.16.0): qr_neon_halo_px (canvas sizing
// caps) + qr_glow_dist_px (rounded-box SDF) + qr_glow_alpha_at (two-layer
// falloff) — the pure math baked into the QR canvas pixels once at build
// time, replacing NEON's old live-LVGL-shadow path. CLOUD PARITY inputs
// (blur_px/spread_px) reuse the same qr_shadow_params concrete values
// already verified in test_qr_style (dim=260, s=0.5 -> blur=36, spread=73).
static void test_qr_neon_glow(void) {
    // --- qr_neon_halo_px: spread_px capped to <= plate_px, then the whole
    // canvas (plate + 2*halo) capped to <= max_side_px. ---
    assert(qr_neon_halo_px(260, 73, 720) == 73);    // spec example: unaffected by either cap
    assert(qr_neon_halo_px(260, 400, 720) == 230);  // spread > plate, but plate+2*halo<=720 binds
                                                     // first: max_halo=(720-260)/2=230 < plate(260)
    assert(qr_neon_halo_px(100, 40, 300) == 40);    // both caps slack -> halo passes through
    assert(qr_neon_halo_px(0,   73, 720) == 0);     // degenerate plate -> 0, no crash
    assert(qr_neon_halo_px(260, -5,  720) == 0);    // negative spread -> 0, no crash
    assert(qr_neon_halo_px(800, 200, 720) == 0);    // plate alone already exceeds max_side_px ->
                                                     // max_halo goes negative, clamped to 0 (no glow,
                                                     // never a negative/undersized canvas)

    // --- qr_glow_dist_px: rounded-box SDF, in the plate's own coord space
    // (0,0)=top-left. rect 100x100 throughout. ---
    assert(qr_glow_dist_px(50, 50, 100, 100, 20) == 0);    // dead center, well inside -> 0
    assert(qr_glow_dist_px(0,  0,  100, 100, 0)  == 0);    // square corner (r=0): the rect's own
                                                            // sharp corner sits exactly on the
                                                            // boundary -> 0 (inside/on-boundary)
    assert(qr_glow_dist_px(0,  0,  100, 100, 20) == 8);    // SAME point, r=20: the sharp geometric
                                                            // corner is now cut away by the rounding,
                                                            // so it reads as just outside the rounded
                                                            // shape (proves the falloff follows the
                                                            // rounded contour, not the bounding box)

    // Flat edge (away from any corner), square corners (r=0): a point 10px
    // past the right edge on the mid-height centerline is exactly 10px away
    // — plain 1D distance, radius doesn't matter on a flat edge.
    assert(qr_glow_dist_px(110, 50, 100, 100, 0)  == 10);
    assert(qr_glow_dist_px(110, 50, 100, 100, 20) == 10);  // same flat-edge point, r=20 -> unchanged
                                                            // (rounding only bites near corners)

    // Diagonal corner point, 10px past each edge: square corners -> plain
    // Euclidean distance sqrt(10^2+10^2)=14.14 -> 14 (NOT the naive
    // Manhattan/max(dx,dy)=10, which would indicate a broken SDF).
    assert(qr_glow_dist_px(110, 110, 100, 100, 0) == 14);
    // Same corner point with r=20: the rounding pulls the boundary inward
    // near the corner, so the point reads FARTHER away than the r=0 case
    // (22 > 14) — proof the falloff actually follows the rounded contour.
    assert(qr_glow_dist_px(110, 110, 100, 100, 20) == 22);

    // Degenerate rect -> 0, no crash/div-by-zero.
    assert(qr_glow_dist_px(5, 5, 0, 100, 20) == 0);
    assert(qr_glow_dist_px(5, 5, 100, 0, 20) == 0);

    // --- qr_glow_alpha_at: two-layer falloff, blur=36/spread=73 (the
    // dim=260,s=0.5 spec example's own qr_shadow_params NEON output). ---
    assert(qr_glow_alpha_at(0, 36, 73) == 255);     // d<=0 (inside the plate) -> full alpha
    assert(qr_glow_alpha_at(-5, 36, 73) == 255);    // negative d (defensive) -> same short-circuit
    // d==blur_px: the tight inner term (a1) has just fully decayed to 0;
    // only the wide outer term (a2) contributes ->
    // 0.55*(1-36/73)^2*255 ~= 36 (comfortably inside the a2-only region,
    // well below the ~128 midpoint of the a1 curve).
    assert(qr_glow_alpha_at(36, 36, 73) == 36);
    int a_near = qr_glow_alpha_at(1, 36, 73);
    assert(a_near > 200 && a_near <= 255);          // just outside the plate -> still near-opaque
    // d==spread_px: both terms have fully decayed -> 0.
    assert(qr_glow_alpha_at(73, 36, 73) == 0);
    assert(qr_glow_alpha_at(200, 36, 73) == 0);     // well past spread -> still 0, no underflow
    // monotonic: alpha never increases as d grows.
    assert(qr_glow_alpha_at(10, 36, 73) >= qr_glow_alpha_at(20, 36, 73));
    assert(qr_glow_alpha_at(20, 36, 73) >= qr_glow_alpha_at(36, 36, 73));
    // degenerate blur/spread (0) -> the respective term never contributes;
    // both 0 with d>0 -> fully transparent, no div-by-zero.
    assert(qr_glow_alpha_at(5, 0, 0) == 0);
    assert(qr_glow_alpha_at(0, 0, 0) == 255);       // d<=0 short-circuit still wins

    printf("test_qr_neon_glow OK\n");
}

static void test_screen_colors(void) {
    device_config_t cfg;

    // 1) valid override on idle -> has_colors true, values parsed.
    // 2) sibling screen with no "colors" key at all -> has_colors false.
    const char *j1 =
        "{\"version\":\"v1\",\"config\":{\"screens\":{"
        "\"idle\":{\"objects\":[],\"colors\":{\"accent\":\"#10a765\",\"bg\":\"#111827\","
        "\"fg\":\"#f9fafb\",\"muted\":\"#9ca3af\"}},"
        "\"processing\":{\"objects\":[]}"
        "}}}";
    assert(cfg_parse_json(j1, &cfg));
    cfg_screen_t *idle = &cfg.screens[SCREEN_IDLE];
    assert(idle->has_colors == true);
    assert(idle->col_bg == 0x111827);
    assert(idle->col_fg == 0xF9FAFB);
    assert(idle->col_accent == 0x10A765);
    assert(idle->col_muted == 0x9CA3AF);
    cfg_screen_t *processing = &cfg.screens[SCREEN_PROCESSING];
    assert(processing->has_colors == false);

    // 3) partial override (only 3 of 4 keys) -> has_colors false.
    const char *j3 =
        "{\"version\":\"v1\",\"config\":{\"screens\":{"
        "\"idle\":{\"objects\":[],\"colors\":{\"accent\":\"#10a765\",\"bg\":\"#111827\","
        "\"fg\":\"#f9fafb\"}}"
        "}}}";
    assert(cfg_parse_json(j3, &cfg));
    assert(cfg.screens[SCREEN_IDLE].has_colors == false);

    // 4) invalid hex in one key ("nope") -> has_colors false.
    const char *j4 =
        "{\"version\":\"v1\",\"config\":{\"screens\":{"
        "\"idle\":{\"objects\":[],\"colors\":{\"accent\":\"nope\",\"bg\":\"#111827\","
        "\"fg\":\"#f9fafb\",\"muted\":\"#9ca3af\"}}"
        "}}}";
    assert(cfg_parse_json(j4, &cfg));
    assert(cfg.screens[SCREEN_IDLE].has_colors == false);

    // 5) colors present but not an object ("colors":"dark") -> has_colors false.
    const char *j5 =
        "{\"version\":\"v1\",\"config\":{\"screens\":{"
        "\"idle\":{\"objects\":[],\"colors\":\"dark\"}"
        "}}}";
    assert(cfg_parse_json(j5, &cfg));
    assert(cfg.screens[SCREEN_IDLE].has_colors == false);

    // 6) '#'-less hex ("10a765") -> accepted (has_colors true).
    const char *j6 =
        "{\"version\":\"v1\",\"config\":{\"screens\":{"
        "\"idle\":{\"objects\":[],\"colors\":{\"accent\":\"10a765\",\"bg\":\"111827\","
        "\"fg\":\"f9fafb\",\"muted\":\"9ca3af\"}}"
        "}}}";
    assert(cfg_parse_json(j6, &cfg));
    assert(cfg.screens[SCREEN_IDLE].has_colors == true);
    assert(cfg.screens[SCREEN_IDLE].col_accent == 0x10A765);
    assert(cfg.screens[SCREEN_IDLE].col_bg     == 0x111827);
    assert(cfg.screens[SCREEN_IDLE].col_fg     == 0xF9FAFB);
    assert(cfg.screens[SCREEN_IDLE].col_muted  == 0x9CA3AF);

    printf("test_screen_colors OK\n");
}

static void test_device_defaults(void) {
    const char *json = "{\"version\":\"v\",\"config\":{}}";   // no "device" block
    device_config_t cfg;
    assert(cfg_parse_json(json, &cfg) && cfg.valid);
    assert(cfg.device.brightness == 100);
    assert(cfg.device.sleep.enabled == false);
    assert(cfg.device.sleep.timeout_seconds == 300);
    assert(cfg.device.settings_password_hash[0] == '\0');
    assert(cfg.device.settings_password_salt[0] == '\0');
    printf("test_device_defaults OK\n");
}

static void test_sha256_and_pin(void) {
    // Known vector: sha256("abc") = ba78...15ad.
    SHA256_CTX c; uint8_t out[32]; char hex[65];
    sha256_init(&c); sha256_update(&c, (const uint8_t*)"abc", 3); sha256_final(&c, out);
    for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", out[i]);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    // settings_pin_verify: salt="a", pin="bc" -> sha256("abc") == the known hash.
    const char *H = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(settings_pin_verify("bc", H, "a") == true);
    assert(settings_pin_verify("xx", H, "a") == false);
    assert(settings_pin_verify("bc", "", "a") == false);   // no PIN configured
    printf("test_sha256_and_pin OK\n");
}

static uint8_t seq_byte(void) { static uint8_t n; return n++; }

static void test_provisioning(void) {
    char code[16];
    assert(provisioning_make_code(code, sizeof(code), seq_byte));
    assert(strlen(code) == 9);
    assert(code[4] == '-');
    const char *ALPHA = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 9; i++) {
        if (i == 4) continue;
        assert(strchr(ALPHA, code[i]) != NULL);
    }
    char tiny[5];
    assert(!provisioning_make_code(tiny, sizeof(tiny), seq_byte));

    char key[64];
    assert(provisioning_parse_claim("{\"status\":\"claimed\",\"deviceKey\":\"dvk_xyz\"}", key, sizeof(key)));
    assert(strcmp(key, "dvk_xyz") == 0);
    assert(!provisioning_parse_claim("{\"status\":\"pending\"}", key, sizeof(key)));
    assert(key[0] == '\0');
    assert(!provisioning_parse_claim("not json", key, sizeof(key)));

    device_config_t pc;
    provisioning_default_setup_config(&pc);
    assert(pc.valid);
    cfg_screen_t *setup = &pc.screens[SCREEN_SETUP];
    bool has_code = false, has_steps = false;
    for (int i = 0; i < setup->n; i++) {
        if (setup->objects[i].type == OBJ_PAIRING_CODE) has_code = true;
        if (setup->objects[i].type == OBJ_STEPS) has_steps = true;
    }
    assert(has_code && has_steps);
    printf("test_provisioning OK\n");
}

static void test_wifi_util(void) {
    wifi_ap_t aps[5] = {
        { "CafeNet", -70, false },
        { "",        -40, true  },
        { "CafeNet", -55, false },
        { "Open",    -80, true  },
        { "Far",     -90, false },
    };
    int n = wifi_util_dedupe_sort(aps, 5);
    assert(n == 3);
    assert(strcmp(aps[0].ssid, "CafeNet") == 0 && aps[0].rssi == -55);
    assert(strcmp(aps[1].ssid, "Open") == 0);
    assert(strcmp(aps[2].ssid, "Far") == 0);
    assert(wifi_util_valid_ssid("x"));
    assert(!wifi_util_valid_ssid(""));
    assert(!wifi_util_valid_ssid("123456789012345678901234567890123"));
    assert(wifi_util_valid_password(true, ""));
    assert(wifi_util_valid_password(false, "12345678"));
    assert(!wifi_util_valid_password(false, "short"));
    assert(!wifi_util_valid_password(false, ""));
    printf("test_wifi_util OK\n");
}

static void test_ota_manifest(void) {
    fw_manifest_t m;
    const char *ok = "{\"version\":\"0.3.0\",\"url\":\"https://r2/x?sig=1\",\"sha256\":\"abc\",\"size\":1599264}";
    assert(ota_parse_manifest(ok, &m));
    assert(strcmp(m.version, "0.3.0") == 0);
    assert(strcmp(m.url, "https://r2/x?sig=1") == 0);
    assert(m.size == 1599264);
    assert(!ota_parse_manifest("{}", &m));
    assert(!ota_parse_manifest("not json", &m));
    assert(!ota_parse_manifest("{\"version\":\"0.3.0\"}", &m));  // missing url

    assert(ota_should_update("0.2.0-m2", "0.3.0"));
    assert(!ota_should_update("0.3.0", "0.3.0"));
    assert(!ota_should_update("0.2.0-m2", ""));
    printf("test_ota_manifest OK\n");
}

static void test_should_sleep(void) {
    // disabled -> never
    assert(should_sleep(100000, 0, 30, false, DEV_IDLE) == false);
    // not idle -> never (even if elapsed)
    assert(should_sleep(100000, 0, 30, true, DEV_QR) == false);
    // idle + enabled, window not elapsed
    assert(should_sleep(10000, 0, 30, true, DEV_IDLE) == false);   // 10s < 30s
    // idle + enabled, window elapsed
    assert(should_sleep(30000, 0, 30, true, DEV_IDLE) == true);    // 30s >= 30s
    assert(should_sleep(45000, 0, 30, true, DEV_IDLE) == true);
    // DEV_PINNED behaves like DEV_IDLE for sleep purposes.
    assert(should_sleep(30000, 0, 30, true, DEV_PINNED) == true);  // elapsed -> sleeps
    assert(should_sleep(10000, 0, 30, true, DEV_PINNED) == false); // not elapsed -> stays awake
    printf("test_should_sleep OK\n");
}

static void test_gesture_swipe_up(void) {
    // clear upward swipe passes
    assert(gesture_is_swipe_up(360, 600, 360, 200, 150) == true);
    // too-short upward travel fails
    assert(gesture_is_swipe_up(360, 300, 360, 220, 150) == false);
    // downward drag fails
    assert(gesture_is_swipe_up(360, 200, 360, 600, 150) == false);
    // mostly-horizontal swipe fails (up=200 but dx=600 > up)
    assert(gesture_is_swipe_up(100, 600, 700, 400, 150) == false);
    // diagonal that's still more-vertical passes
    assert(gesture_is_swipe_up(360, 600, 300, 250, 150) == true);
    printf("test_gesture_swipe_up OK\n");
}

static void test_devset(void) {
    device_settings_t d; devset_init(&d);
    // init: use_device off, both values unset, effective follows cloud.
    assert(d.use_device == 0 && d.brightness == -1 && d.sleep == -1);
    assert(devset_eff_brightness(&d, 54) == 54);
    assert(devset_eff_sleep(&d, 300) == 300);

    // Setting device values while OFF does not affect the effective value.
    devset_set_brightness(&d, 80);
    devset_set_sleep(&d, 0);
    assert(d.brightness == 80 && d.sleep == 0);
    assert(devset_eff_brightness(&d, 54) == 54);   // use_device still 0 -> follows cloud
    assert(devset_eff_sleep(&d, 300) == 300);

    // Turning the source ON now uses the device values (already set, not reseeded).
    devset_set_use_device(&d, 1, 33, 600);
    assert(d.use_device == 1 && d.brightness == 80 && d.sleep == 0);
    assert(devset_eff_brightness(&d, 54) == 80);
    assert(devset_eff_sleep(&d, 300) == 0);

    // Turning OFF -> effective follows cloud again, device values retained.
    devset_set_use_device(&d, 0, 33, 600);
    assert(d.use_device == 0 && d.brightness == 80 && d.sleep == 0);
    assert(devset_eff_brightness(&d, 54) == 54);
    assert(devset_eff_sleep(&d, 300) == 300);

    // Enabling with UNSET (-1) device values seeds them from the cloud so
    // nothing jumps.
    device_settings_t e; devset_init(&e);
    devset_set_use_device(&e, 1, 42, 120);
    assert(e.use_device == 1 && e.brightness == 42 && e.sleep == 120);
    assert(devset_eff_brightness(&e, 99) == 42);   // device value wins
    assert(devset_eff_sleep(&e, 999) == 120);
    // Editing the seeded values then takes effect immediately.
    devset_set_brightness(&e, 15);
    devset_set_sleep(&e, 60);
    assert(devset_eff_brightness(&e, 99) == 15);
    assert(devset_eff_sleep(&e, 999) == 60);
    printf("test_devset OK\n");
}

static void test_brightness(void) {
    // Gamma 0.5 perceptual curve: duty = round(100*(level/100)^0.5), clamped 10..100.
    assert(brightness_level_to_duty(100) == 100);
    assert(brightness_level_to_duty(50)  == 71);   // 100*sqrt(0.5)=70.71 -> 71
    assert(brightness_level_to_duty(30)  == 55);   // 100*sqrt(0.3)=54.77 -> 55
    assert(brightness_level_to_duty(10)  == 32);   // 100*sqrt(0.1)=31.62 -> 32
    // clamps: below 10 -> level 10; above 100 -> level 100.
    assert(brightness_level_to_duty(0)   == 32);   // clamped up to level 10
    assert(brightness_level_to_duty(200) == 100);  // clamped down to level 100
    // monotonic non-decreasing across the valid range.
    int prev = brightness_level_to_duty(10);
    for (int lvl = 11; lvl <= 100; lvl++) {
        int cur = brightness_level_to_duty(lvl);
        assert(cur >= prev);
        prev = cur;
    }
    printf("test_brightness OK\n");
}

static void test_gesture_snap(void) {
    // short drag, no flick -> dismiss (100/720 = 14%)
    assert(gesture_snap(100, 720, 0, 40, 1200) == SNAP_DISMISS);
    // >=40% revealed -> open (300/720 = 41.7%)
    assert(gesture_snap(300, 720, 0, 40, 1200) == SNAP_OPEN);
    // exactly 40% -> open (288/720 = 40%)
    assert(gesture_snap(288, 720, 0, 40, 1200) == SNAP_OPEN);
    // fast upward flick under 40% -> open
    assert(gesture_snap(120, 720, 2000, 40, 1200) == SNAP_OPEN);
    // slow flick under 40% -> dismiss
    assert(gesture_snap(120, 720, 500, 40, 1200) == SNAP_DISMISS);
    // no movement -> dismiss
    assert(gesture_snap(0, 720, 0, 40, 1200) == SNAP_DISMISS);
    printf("test_gesture_snap OK\n");
}

static void test_trigger_show_qr(void) {
    cJSON *ok = cJSON_Parse("{\"type\":\"trigger\",\"action\":\"show_qr\",\"payload\":{\"url\":\"https://e.co/r/x\"}}");
    assert(ok);
    const char *u = command_trigger_show_qr_url(ok);
    assert(u && strcmp(u, "https://e.co/r/x") == 0);
    cJSON_Delete(ok);

    cJSON *nourl = cJSON_Parse("{\"action\":\"show_qr\",\"payload\":{}}");
    assert(command_trigger_show_qr_url(nourl) == NULL);
    cJSON_Delete(nourl);

    cJSON *empty = cJSON_Parse("{\"action\":\"show_qr\",\"payload\":{\"url\":\"\"}}");
    assert(command_trigger_show_qr_url(empty) == NULL);
    cJSON_Delete(empty);

    cJSON *other = cJSON_Parse("{\"action\":\"print\",\"payload\":{\"url\":\"https://e.co\"}}");
    assert(command_trigger_show_qr_url(other) == NULL);
    cJSON_Delete(other);

    cJSON *nopayload = cJSON_Parse("{\"action\":\"show_qr\"}");
    assert(command_trigger_show_qr_url(nopayload) == NULL);
    cJSON_Delete(nopayload);

    printf("test_trigger_show_qr OK\n");
}

static void test_mqtt_block(void) {
    // present + full → enabled, fields copied, port kept
    const char *j1 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"b.example.com\",\"port\":8883,\"clientId\":\"dev_9\",\"username\":\"dev_9\"}}";
    device_config_t c1; assert(cfg_parse_json(j1, &c1));
    assert(c1.mqtt.enabled);
    assert(strcmp(c1.mqtt.host, "b.example.com") == 0);
    assert(c1.mqtt.port == 8883);
    assert(strcmp(c1.mqtt.client_id, "dev_9") == 0);
    assert(strcmp(c1.mqtt.username, "dev_9") == 0);

    // absent → disabled
    const char *j2 = "{\"version\":\"1\"}";
    device_config_t c2; assert(cfg_parse_json(j2, &c2));
    assert(!c2.mqtt.enabled);

    // present but empty host → disabled
    const char *j3 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"\",\"username\":\"dev_9\"}}";
    device_config_t c3; assert(cfg_parse_json(j3, &c3));
    assert(!c3.mqtt.enabled);

    // host present, port omitted → default 8883
    const char *j4 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"h\",\"clientId\":\"d\",\"username\":\"d\"}}";
    device_config_t c4; assert(cfg_parse_json(j4, &c4));
    assert(c4.mqtt.enabled && c4.mqtt.port == 8883);

    printf("test_mqtt_block OK\n");
}

static void test_pin_block(void) {
    // (a) pin.url present + non-empty -> pin_set true, url copied.
    const char *j1 = "{\"version\":\"1\",\"pin\":{\"url\":\"https://x/menu\"}}";
    device_config_t c1; assert(cfg_parse_json(j1, &c1));
    assert(c1.pin_set == true);
    assert(strcmp(c1.pin_url, "https://x/menu") == 0);

    // (b) pin explicitly null -> pin_set false.
    const char *j2 = "{\"version\":\"1\",\"pin\":null}";
    device_config_t c2; assert(cfg_parse_json(j2, &c2));
    assert(c2.pin_set == false);

    // (c) pin absent entirely -> pin_set false.
    const char *j3 = "{\"version\":\"1\"}";
    device_config_t c3; assert(cfg_parse_json(j3, &c3));
    assert(c3.pin_set == false);

    // (d) pin.url present but empty string -> pin_set false.
    const char *j4 = "{\"version\":\"1\",\"pin\":{\"url\":\"\"}}";
    device_config_t c4; assert(cfg_parse_json(j4, &c4));
    assert(c4.pin_set == false);

    // (e) 600-char URL -> truncated to 511 chars + NUL, pin_set still true.
    char longurl[601];
    memset(longurl, 'a', 600); longurl[600] = '\0';
    char j5[700];
    snprintf(j5, sizeof(j5), "{\"version\":\"1\",\"pin\":{\"url\":\"%s\"}}", longurl);
    device_config_t c5; assert(cfg_parse_json(j5, &c5));
    assert(c5.pin_set == true);
    assert(strlen(c5.pin_url) == 511);
    assert(c5.pin_url[511] == '\0');

    // (f) re-parsing a pin-less config into an already-pinned cfg clears
    // pin_set (cfg_parse_json memsets on entry, so a stale pin can't leak
    // forward into the next poll's config once the cloud drops it).
    const char *j6 = "{\"version\":\"1\"}";
    assert(cfg_parse_json(j6, &c1));   // c1 was pinned by (a) above
    assert(c1.pin_set == false);

    // (g) pin is not an object (e.g. a bare number) -> ignored, pin_set false.
    const char *j7 = "{\"version\":\"1\",\"pin\":42}";
    device_config_t c7; assert(cfg_parse_json(j7, &c7));
    assert(c7.pin_set == false);

    // (h) pin.url present but not a string -> ignored, pin_set false.
    const char *j8 = "{\"version\":\"1\",\"pin\":{\"url\":123}}";
    device_config_t c8; assert(cfg_parse_json(j8, &c8));
    assert(c8.pin_set == false);

    printf("test_pin_block OK\n");
}

static void test_mqtt_topics(void) {
    char t[64];
    mqtt_topic_cmd(t, sizeof(t), "dev_9"); assert(strcmp(t, "d/dev_9/cmd") == 0);
    mqtt_topic_ack(t, sizeof(t), "dev_9"); assert(strcmp(t, "d/dev_9/ack") == 0);
    mqtt_topic_hb(t, sizeof(t), "dev_9");  assert(strcmp(t, "d/dev_9/hb") == 0);
    // The broker ACL is keyed on this exact shape (username == device id); a
    // silent change here would be rejected by EMQX with no local failure.
    mqtt_topic_cfg_get(t, sizeof(t), "dev_9"); assert(strcmp(t, "d/dev_9/cfg/get") == 0);
    printf("test_mqtt_topics OK\n");
}

static void test_command_parse_into(void) {
    dev_command_t c;
    cJSON *ok = cJSON_Parse("{\"id\":\"cmd_1\",\"type\":\"trigger\",\"action\":\"show_qr\",\"payload\":{\"url\":\"https://x/y\"}}");
    assert(command_parse_into(ok, &c));
    assert(strcmp(c.command_id, "cmd_1") == 0);
    assert(strcmp(c.type, "trigger") == 0);
    assert(strcmp(c.action, "show_qr") == 0);
    assert(strcmp(c.url, "https://x/y") == 0);
    cJSON_Delete(ok);

    // non-trigger: type/id copied, url empty
    cJSON *rb = cJSON_Parse("{\"id\":\"cmd_2\",\"type\":\"reboot\"}");
    assert(command_parse_into(rb, &c));
    assert(strcmp(c.type, "reboot") == 0 && c.url[0] == '\0');
    cJSON_Delete(rb);

    // missing id or type → false
    cJSON *bad = cJSON_Parse("{\"type\":\"reboot\"}");
    assert(!command_parse_into(bad, &c));
    cJSON_Delete(bad);

    // MQTT format: command id arrives as "commandId" (not "id")
    cJSON *mq = cJSON_Parse("{\"commandId\":\"cmd_9\",\"type\":\"trigger\",\"action\":\"show_qr\",\"payload\":{\"url\":\"https://a/b\"}}");
    assert(command_parse_into(mq, &c));
    assert(strcmp(c.command_id, "cmd_9") == 0);
    assert(strcmp(c.url, "https://a/b") == 0);
    cJSON_Delete(mq);

    printf("test_command_parse_into OK\n");
}

static void test_cmd_envelope(void) {
    char *p = NULL;

    // Carried config: payload comes back as JSON text and feeds cfg_parse_json.
    const char *env = "{\"commandId\":\"cmd_1\",\"type\":\"config-changed\",\"action\":null,"
                      "\"payload\":{\"version\":\"v9\",\"brandColor\":\"#10A765\"}}";
    assert(cmd_payload_json(env, &p));
    assert(p && strstr(p, "\"version\"") && strstr(p, "v9"));
    cJSON_free(p); p = NULL;

    // The legacy nudge. MUST be false so the caller re-requests over cfg/get
    // instead of reporting a parse failure.
    assert(!cmd_payload_json("{\"commandId\":\"c\",\"type\":\"config-changed\",\"payload\":null}", &p));
    assert(p == NULL);

    // Missing payload, non-object payload, not an object, malformed.
    assert(!cmd_payload_json("{\"commandId\":\"c\",\"type\":\"config-changed\"}", &p));
    assert(!cmd_payload_json("{\"payload\":42}", &p));
    assert(!cmd_payload_json("[1,2,3]", &p));
    assert(!cmd_payload_json("not json", &p));
    assert(!cmd_payload_json("", &p));

    // Round trip into the REAL config parser — proves the extracted text is
    // exactly what cfg_parse_json expects, not merely well-formed JSON.
    const char *full = "{\"commandId\":\"cmd_2\",\"type\":\"config-changed\",\"payload\":"
        "{\"version\":\"v3\",\"brandColor\":\"#10A765\",\"wordmark\":\"Acme\","
        "\"config\":{\"clockTimezone\":\"UTC\",\"qrTimeoutSeconds\":15,\"screens\":{}},"
        "\"device\":{\"brightness\":70,\"sleep\":{\"enabled\":false,\"timeoutSeconds\":300}},"
        "\"pin\":null}}";
    assert(cmd_payload_json(full, &p));
    static device_config_t cfg;
    assert(cfg_parse_json(p, &cfg));
    assert(strcmp(cfg.etag, "v3") == 0);
    assert(cfg.device.brightness == 70);
    cJSON_free(p); p = NULL;

    // Same helper, OTA manifest payload — one extractor, both consumers.
    const char *ota = "{\"commandId\":\"cmd_3\",\"type\":\"firmware-update\",\"payload\":"
        "{\"version\":\"0.18.0\",\"url\":\"https://r2/x?sig=1\",\"sha256\":\"abc\",\"size\":1599264}}";
    assert(cmd_payload_json(ota, &p));
    fw_manifest_t m;
    assert(ota_parse_manifest(p, &m));
    assert(strcmp(m.version, "0.18.0") == 0);
    assert(m.size == 1599264);
    cJSON_free(p); p = NULL;

    printf("test_cmd_envelope OK\n");
}

int main(void) {
    test_parse();
    test_geom();
    test_qr_style();
    test_zsort();
    test_map();
    test_screen_for_pinned();
    test_pinned_screen_parse();
    test_home_state();
    test_asset_key();
    test_asset_evict();
    test_clock();
    test_mmss();
    test_qr_timeout();
    test_qr_shape_colors();
    test_qr_corner_shadow();
    test_qr_shadow_fields();
    test_qr_neon_glow();
    test_screen_colors();
    test_device_defaults();
    test_pin_block();
    test_mqtt_block();
    test_mqtt_topics();
    test_provisioning();
    test_wifi_util();
    test_ota_manifest();
    test_should_sleep();
    test_sha256_and_pin();
    test_gesture_swipe_up();
    test_gesture_snap();
    test_devset();
    test_brightness();
    test_trigger_show_qr();
    test_command_parse_into();
    test_cmd_envelope();
    printf("ALL TESTS PASSED\n");
    return 0;
}
