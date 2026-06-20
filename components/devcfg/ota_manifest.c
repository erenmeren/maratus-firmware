#include "ota_manifest.h"
#include <string.h>
#include "cJSON.h"

static void copy_str(char *dst, size_t cap, const char *src) {
    dst[0] = '\0';
    if (src) { strncpy(dst, src, cap - 1); dst[cap - 1] = '\0'; }
}

bool ota_parse_manifest(const char *json, fw_manifest_t *out) {
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    const char *u = cJSON_GetStringValue(cJSON_GetObjectItem(root, "url"));
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(root, "sha256"));
    cJSON *sz = cJSON_GetObjectItem(root, "size");
    bool ok = v && v[0] && u && u[0];
    if (ok) {
        copy_str(out->version, sizeof(out->version), v);
        copy_str(out->url, sizeof(out->url), u);
        copy_str(out->sha256, sizeof(out->sha256), s);
        out->size = cJSON_IsNumber(sz) ? (int)sz->valuedouble : 0;
    }
    cJSON_Delete(root);
    return ok;
}

bool ota_should_update(const char *running_version, const char *manifest_version) {
    if (!manifest_version || !manifest_version[0]) return false;
    if (!running_version) return true;
    return strcmp(running_version, manifest_version) != 0;
}
