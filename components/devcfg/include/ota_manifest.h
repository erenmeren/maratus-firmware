#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char version[32];
    char url[600];
    char sha256[72];
    int  size;
} fw_manifest_t;

// Parse GET /api/device/firmware JSON into *out. True only if version + url are
// present and non-empty. Empty/{}/malformed → false.
bool ota_parse_manifest(const char *json, fw_manifest_t *out);

// True if manifest version is non-empty AND differs from the running version.
bool ota_should_update(const char *running_version, const char *manifest_version);
