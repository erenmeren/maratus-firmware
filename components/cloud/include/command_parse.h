#pragma once
#include "cJSON.h"

// Pure: if `cmd` has action=="show_qr" and a non-empty payload.url string, return
// that url (pointer into the cJSON tree, valid until cJSON_Delete). Else NULL.
const char *command_trigger_show_qr_url(const cJSON *cmd);
