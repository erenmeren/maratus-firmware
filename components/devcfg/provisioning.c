#include "provisioning.h"
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_mac.h"

static const char ALPHA[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 chars, no ambiguous

bool provisioning_make_code(char *out, size_t out_len, uint8_t (*rand_byte)(void)) {
    if (!out || out_len < 10 || !rand_byte) return false;
    int j = 0;
    for (int i = 0; i < 8; i++) {
        if (i == 4) out[j++] = '-';
        out[j++] = ALPHA[rand_byte() & 31];
    }
    out[j] = '\0';
    return true;
}

bool provisioning_parse_claim(const char *json, char *key_out, size_t key_len) {
    if (key_out && key_len) key_out[0] = '\0';
    if (!json || !key_out || key_len == 0) return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = false;
    const char *k = cJSON_GetStringValue(cJSON_GetObjectItem(root, "deviceKey"));
    if (k && k[0]) {
        strncpy(key_out, k, key_len - 1);
        key_out[key_len - 1] = '\0';
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

void provisioning_default_setup_config(device_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->brand_bg = 0x0B5D3B;
    cfg->brand_fg = 0xFFFFFF;
    cfg->brand_accent = 0x10A765;
    cfg_screen_t *s = &cfg->screens[SCREEN_SETUP];
    int i = 0;
    cfg_object_t *t = &s->objects[i++];
    t->type = OBJ_TEXT; t->visible = true; t->x = 0.1f; t->y = 0.14f; t->w = 0.8f; t->h = 0.08f;
    t->font_size = 34; t->align = CFG_ALIGN_CENTER; t->z = 0;
    strncpy(t->text, "Pair this device", CFG_TEXT_LEN - 1);
    cfg_object_t *st = &s->objects[i++];
    st->type = OBJ_STEPS; st->visible = true; st->x = 0.18f; st->y = 0.34f; st->w = 0.64f; st->h = 0.28f; st->z = 1;
    cfg_object_t *pc = &s->objects[i++];
    pc->type = OBJ_PAIRING_CODE; pc->visible = true; pc->x = 0.2f; pc->y = 0.66f; pc->w = 0.6f; pc->h = 0.14f; pc->z = 2;
    s->n = i;
    cfg->valid = true;
}

// See provisioning.h for the eFuse-vs-wifi-MAC trap this must not repeat.
void provisioning_get_serial(char out[13]) {
    uint8_t mac[6] = {0};
    out[0] = '\0';
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(out, 13, "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}
