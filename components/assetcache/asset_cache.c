#include "asset_cache.h"
#include "asset_key.h"
#include "cloud.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "assetcache";

#define ASSET_BASE "/assets"
#define KEY_LEN    40
// "/assets/" + 40 hex + ".png" + ".tmp" + NUL — comfortably bounded.
#define PATH_MAX_LEN 80

// Build /assets/<key>.png (or .png.tmp when tmp != 0) into dst[PATH_MAX_LEN].
static void key_path(const char *key, char *dst, int tmp)
{
    snprintf(dst, PATH_MAX_LEN, "%s/%s.png%s", ASSET_BASE, key, tmp ? ".tmp" : "");
}

// Read an existing file fully into a fresh MALLOC_CAP_SPIRAM buffer.
static esp_err_t read_file(const char *path, uint8_t **out, size_t *out_len)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) return ESP_ERR_NOT_FOUND;

    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    size_t n = (size_t)st.st_size;
    uint8_t *buf = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t rd = fread(buf, 1, n, f);
    fclose(f);
    if (rd != n) { heap_caps_free(buf); return ESP_FAIL; }

    *out = buf;
    *out_len = n;
    return ESP_OK;
}

// Write bytes to /assets/<key>.png via a temp file + rename (atomic-ish).
static void store_file(const char *key, const uint8_t *data, size_t len)
{
    char tmp[PATH_MAX_LEN], dst[PATH_MAX_LEN];
    key_path(key, tmp, 1);
    key_path(key, dst, 0);

    FILE *f = fopen(tmp, "wb");
    if (!f) { ESP_LOGW(TAG, "store: open %s failed", tmp); return; }
    size_t wr = fwrite(data, 1, len, f);
    fclose(f);
    if (wr != len) {
        ESP_LOGW(TAG, "store: short write %u/%u", (unsigned)wr, (unsigned)len);
        unlink(tmp);
        return;
    }
    if (rename(tmp, dst) != 0) {
        ESP_LOGW(TAG, "store: rename %s failed", dst);
        unlink(tmp);
    }
}

esp_err_t asset_cache_get(const char *url, uint8_t **out, size_t *out_len)
{
    if (!url || !out || !out_len) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    char key[41];
    asset_key(url, key);
    if (key[0] == '\0') return ESP_ERR_INVALID_ARG;

    char path[PATH_MAX_LEN];
    key_path(key, path, 0);

    // Cache hit: serve from flash, no network.
    if (read_file(path, out, out_len) == ESP_OK) {
        ESP_LOGI(TAG, "hit %s (%u bytes)", key, (unsigned)*out_len);
        return ESP_OK;
    }

    // Cache miss: fetch over HTTPS, then persist.
    uint8_t *bytes = NULL;
    size_t n = 0;
    esp_err_t err = cloud_fetch_asset(url, &bytes, &n);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "miss %s: fetch failed (%s)", key, esp_err_to_name(err));
        return err;
    }

    store_file(key, bytes, n);   // best-effort; still return the bytes on failure
    ESP_LOGI(TAG, "miss %s: fetched %u bytes", key, (unsigned)n);

    *out = bytes;
    *out_len = n;
    return ESP_OK;
}

esp_err_t asset_cache_read_cached(const char *url, uint8_t **out, size_t *out_len)
{
    if (!url || !out || !out_len) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    char key[41];
    asset_key(url, key);
    if (key[0] == '\0') return ESP_ERR_INVALID_ARG;

    char path[PATH_MAX_LEN];
    key_path(key, path, 0);

    // Flash only — no cloud_fetch_asset fallback. Miss => ESP_ERR_NOT_FOUND.
    return read_file(path, out, out_len);
}

// Is `key` present in referenced[0..n)?
static int is_referenced(const char *key, const char *const *referenced, int n)
{
    for (int i = 0; i < n; i++) {
        if (referenced[i] && strcmp(key, referenced[i]) == 0) return 1;
    }
    return 0;
}

void asset_cache_evict(const char *const *referenced_keys, int n)
{
    if (!referenced_keys) n = 0;

    DIR *d = opendir(ASSET_BASE);
    if (!d) { ESP_LOGW(TAG, "evict: opendir %s failed", ASSET_BASE); return; }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        size_t nl = strlen(name);
        // Expect exactly "<40 hex>.png".
        if (nl != KEY_LEN + 4) continue;
        if (strcmp(name + KEY_LEN, ".png") != 0) continue;

        char key[41];
        memcpy(key, name, KEY_LEN);
        key[KEY_LEN] = '\0';

        if (is_referenced(key, referenced_keys, n)) continue;

        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/%s", ASSET_BASE, name);
        if (unlink(path) == 0) ESP_LOGI(TAG, "evicted %s", key);
        else ESP_LOGW(TAG, "evict: unlink %s failed", path);
    }
    closedir(d);
}
