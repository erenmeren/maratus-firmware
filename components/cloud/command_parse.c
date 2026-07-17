#include "command_parse.h"
#include <string.h>
#include <stdio.h>

const char *command_trigger_show_qr_url(const cJSON *cmd) {
    if (!cmd) return NULL;
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "action"));
    if (!action || strcmp(action, "show_qr") != 0) return NULL;
    const cJSON *payload = cJSON_GetObjectItem((cJSON *)cmd, "payload");
    if (!payload) return NULL;
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)payload, "url"));
    if (!url || url[0] == '\0') return NULL;
    return url;
}

static void copy_field(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

bool command_parse_into(const cJSON *cmd, dev_command_t *out) {
    if (!cmd || !out) return false;
    // MQTT command payloads use "commandId"; HTTP poll-response commands use "id".
    // Accept either so the same parser serves both transports.
    const char *id   = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "commandId"));
    if (!id) id      = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "id"));
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "type"));
    if (!id || !type) return false;
    copy_field(out->command_id, sizeof(out->command_id), id);
    copy_field(out->type, sizeof(out->type), type);
    copy_field(out->action, sizeof(out->action), cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "action")));
    const char *url = command_trigger_show_qr_url(cmd);   // NULL unless show_qr + payload.url
    copy_field(out->url, sizeof(out->url), url);
    return true;
}
