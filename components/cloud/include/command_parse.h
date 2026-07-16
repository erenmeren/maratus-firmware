#pragma once
#include "cJSON.h"
#include <stdbool.h>

// Pure: if `cmd` has action=="show_qr" and a non-empty payload.url string, return
// that url (pointer into the cJSON tree, valid until cJSON_Delete). Else NULL.
const char *command_trigger_show_qr_url(const cJSON *cmd);

// A command copied out of the transient cJSON tree so it can cross a task
// boundary and be dispatched transport-independently.
typedef struct {
    char command_id[40];
    char type[24];
    char action[24];
    char url[512];   // payload.url for action==show_qr; "" otherwise
} dev_command_t;

// Parse one command object into `out`. Returns false if id or type is missing.
bool command_parse_into(const cJSON *cmd, dev_command_t *out);
