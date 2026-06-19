#include "provisioning.h"
#include <string.h>
#include "cJSON.h"

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
