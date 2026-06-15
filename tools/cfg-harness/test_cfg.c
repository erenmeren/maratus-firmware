#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "cfg_parse.h"
#include "render_geom.h"

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

    // idle: hidden object dropped -> 1 visible text.
    cfg_screen_t *idle = &cfg.screens[SCREEN_IDLE];
    assert(idle->n == 1);
    assert(idle->objects[0].type == OBJ_TEXT);
    assert(strcmp(idle->objects[0].text, "Welcome") == 0);
    assert(idle->objects[0].font_size == 64);
    assert(idle->objects[0].align == CFG_ALIGN_CENTER);

    // qr: a qr widget + a text.
    cfg_screen_t *qr = &cfg.screens[SCREEN_QR];
    assert(qr->n == 2);

    // setup: pairingCode widget parsed.
    assert(cfg.screens[SCREEN_SETUP].objects[0].type == OBJ_PAIRING_CODE);

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
    printf("test_map OK\n");
}

int main(void) {
    test_parse();
    test_geom();
    test_zsort();
    test_map();
    printf("ALL TESTS PASSED\n");
    return 0;
}
