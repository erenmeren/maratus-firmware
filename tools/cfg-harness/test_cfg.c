#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "cfg_parse.h"
#include "render_geom.h"
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

    // below min (5) -> clamped to 15
    const char *j3 = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":5}}";
    assert(cfg_parse_json(j3, &cfg) && cfg.qr_timeout_seconds == 15);

    // above max (999) -> clamped to 180
    const char *j4 = "{\"version\":\"v1\",\"config\":{\"qrTimeoutSeconds\":999}}";
    assert(cfg_parse_json(j4, &cfg) && cfg.qr_timeout_seconds == 180);

    printf("test_qr_timeout OK\n");
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

int main(void) {
    test_parse();
    test_geom();
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
    printf("ALL TESTS PASSED\n");
    return 0;
}
