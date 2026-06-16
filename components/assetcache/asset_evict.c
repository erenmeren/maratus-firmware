#include "asset_evict.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool is_referenced(const char *key,
                          const char *const *referenced, int n_ref) {
    if (!key) return false;
    for (int i = 0; i < n_ref; i++) {
        if (referenced[i] && strcmp(key, referenced[i]) == 0) return true;
    }
    return false;
}

int asset_evict_list(const char *const *on_flash, int n_flash,
                     const char *const *referenced, int n_ref,
                     const char **out_delete, int max_out) {
    if (!on_flash || n_flash <= 0 || !out_delete || max_out <= 0) return 0;
    if (!referenced) n_ref = 0;

    int out = 0;
    for (int i = 0; i < n_flash && out < max_out; i++) {
        const char *key = on_flash[i];
        if (!key) continue;
        // Duplicate referenced entries collapse naturally: membership is a
        // boolean, so a key referenced twice is still simply "referenced".
        if (!is_referenced(key, referenced, n_ref)) {
            out_delete[out++] = key;
        }
    }
    return out;
}
