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
    assert(idle->objects[0].align == ALIGN_CENTER);

    // qr: a qr widget + a text.
    cfg_screen_t *qr = &cfg.screens[SCREEN_QR];
    assert(qr->n == 2);

    // setup: pairingCode widget parsed.
    assert(cfg.screens[SCREEN_SETUP].objects[0].type == OBJ_PAIRING_CODE);

    free(json);
    printf("test_parse OK\n");
}

int main(void) {
    test_parse();
    printf("ALL TESTS PASSED\n");
    return 0;
}
