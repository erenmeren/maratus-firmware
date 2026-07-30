#include "cmd_envelope.h"
#include "cJSON.h"

bool cmd_payload_json(const char *envelope, char **out)
{
    if (!envelope || !envelope[0] || !out) return false;
    cJSON *root = cJSON_Parse(envelope);
    if (!root) return false;
    bool ok = false;
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        char *s = cJSON_PrintUnformatted(payload);
        if (s) { *out = s; ok = true; }
    }
    cJSON_Delete(root);
    return ok;
}
