#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// On-flash asset cache, backed by the `assets` LittleFS partition mounted at
// /assets (see app_main). Keys are the 40-char hex digests from asset_key();
// each cached object is stored as /assets/<key>.png. This layer is I/O only —
// no image decode or rendering happens here.

// Resolve an asset URL to its bytes.
//   - Cache hit: reads /assets/<key>.png into a fresh MALLOC_CAP_SPIRAM buffer,
//     no network.
//   - Cache miss: cloud_fetch_asset(url) → stores the bytes to /assets/<key>.png
//     (write-temp-then-rename) → returns those bytes.
// On ESP_OK, *out points to *out_len bytes the CALLER must heap_caps_free.
// Returns an esp_err_t != ESP_OK on bad args, fetch failure, or alloc failure
// (a failed store is logged but still returns the fetched bytes).
esp_err_t asset_cache_get(const char *url, uint8_t **out, size_t *out_len);

// Prune the cache: unlink every /assets/<key>.png whose key (filename minus the
// .png suffix) is NOT present in referenced_keys[0..n). NULL/zero n removes all.
void asset_cache_evict(const char *const *referenced_keys, int n);
