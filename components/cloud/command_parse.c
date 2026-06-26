#include "command_parse.h"
#include <string.h>

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
