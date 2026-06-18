#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cloud.h"
#include "commands.h"
#include "net.h"
#include "ui.h"
#include "assets.h"
#include "device_config.h"
#include "asset_cache.h"
#include "asset_key.h"
#include "esp_heap_caps.h"
#include "time_sync.h"
#include "esp_wifi.h"

static const char *TAG = "state";
static dev_state_t s_state = DEV_BOOT;
// Double-buffered config (parse into the idle buffer, publish under the LVGL lock).
// Each device_config_t is ~67KB, so both live in PSRAM (CPU-read only, no DMA) to
// keep internal DRAM free for Wi-Fi/SDIO/TLS buffers — allocated in app_state_run().
static device_config_t *s_cfg_buf[2];
static int s_cfg_live = 0;             // index of the buffer the renderer currently points at
static volatile bool s_config_dirty = true;   // fetch once after first connect

void app_state_request_config(void) { s_config_dirty = true; }

#define POLL_IDLE_MS     12000
#define POLL_BACKOFF_MIN 2000
#define POLL_BACKOFF_MAX 60000

// M3: upload the bundled test PNG and show the returned receipt URL as a QR.
// Replaced by the live ESC/POS render path in M4.
static void run_test_ingest(void)
{
    ui_render_state(DEV_PROCESSING);
    s_state = DEV_PROCESSING;

    size_t len = 0;
    const uint8_t *png = assets_test_png(&len);

    static char url[256];
    int status = cloud_post_receipt(png, len, url, sizeof(url));
    if (status == 201 && url[0]) {
        ESP_LOGI(TAG, "receipt ready: %s", url);
        s_state = DEV_QR;
        ui_set_qr_url(url);
        ui_set_countdown(s_cfg_buf[s_cfg_live]->qr_timeout_seconds);
        ui_render_state(DEV_QR);
    } else {
        ESP_LOGW(TAG, "ingest failed (status=%d)", status);
        s_state = DEV_IDLE;
        ui_render_state(DEV_IDLE);
    }
}

// Most configs reference only a logo + a handful of uploaded icons; cap well
// under CFG_MAX_OBJECTS * SCREEN_COUNT to bound stack use in the prefetch frame.
#define MAX_ASSET_REFS 16

// Collect the uploaded-image asset URLs a config references: the top-level
// logo_url, plus every visible OBJ_ICON with icon_src==ICON_UPLOAD across all
// screens. urls[] alias into cfg (no copy). Returns the count (<= cap).
static int gather_asset_urls(const device_config_t *cfg, const char *urls[], int cap)
{
    int n = 0;
    if (cfg->logo_url[0] && n < cap) urls[n++] = cfg->logo_url;
    for (int s = 0; s < SCREEN_COUNT && n < cap; s++) {
        const cfg_screen_t *sc = &cfg->screens[s];
        for (int i = 0; i < sc->n && n < cap; i++) {
            const cfg_object_t *o = &sc->objects[i];
            if (o->type == OBJ_ICON && o->icon_src == ICON_UPLOAD &&
                o->visible && o->icon_url[0]) {
                urls[n++] = o->icon_url;
            }
        }
    }
    return n;
}

// On config apply (poll task — network OK here): fetch+store every referenced
// uploaded image so the cache-only render path gets hits, then evict any cached
// asset the new config no longer references (rebrand cleanup). Individual fetch
// failures are ignored: offline assets simply stay placeholders on screen.
static void prefetch_and_evict_assets(const device_config_t *cfg)
{
    const char *urls[MAX_ASSET_REFS];
    int n = gather_asset_urls(cfg, urls, MAX_ASSET_REFS);

    static char keys[MAX_ASSET_REFS][41];
    const char *key_ptrs[MAX_ASSET_REFS];
    for (int i = 0; i < n; i++) {
        uint8_t *buf = NULL;
        size_t   len = 0;
        if (asset_cache_get(urls[i], &buf, &len) == ESP_OK) {
            heap_caps_free(buf);   // we only needed it persisted to flash
        }
        asset_key(urls[i], keys[i]);
        key_ptrs[i] = keys[i];
    }
    asset_cache_evict(key_ptrs, n);
}

// Map RSSI (dBm) to 0..4 signal bars.
static int rssi_to_level(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

// Wait up to total_ms, but break early (~500ms granularity) to auto-return from
// the receipt screen when its countdown expires. Avoids extra cloud polls.
static void idle_wait_or_qr_expiry(int total_ms) {
    const int slice = 500;
    for (int waited = 0; waited < total_ms; waited += slice) {
        if (ui_consume_countdown_expired()) {
            s_state = DEV_IDLE;
            ui_render_state(DEV_IDLE);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(slice));
    }
}

static void poll_task(void *arg)
{
    static char body[1024];
    int backoff = POLL_BACKOFF_MIN;
    s_state = DEV_IDLE;

    for (;;) {
        if (ui_consume_tap() && net_is_connected()) {
            run_test_ingest();
            continue;
        }
        if (!net_is_connected()) {
            ui_set_online(false);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        int status = cloud_get_commands(body, sizeof(body));
        if (status == 200) {
            ui_set_online(true);
            time_sync_start();                 // idempotent; SNTP runs once Wi-Fi is up
            int rssi = 0;
            if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) ui_set_wifi_level(rssi_to_level(rssi));
            commands_handle_body(body);   // may esp_restart() on reboot
            if (s_config_dirty) {
                s_config_dirty = false;
                int next = s_cfg_live ^ 1;
                *s_cfg_buf[next] = *s_cfg_buf[s_cfg_live];   // seed ETag + 304 fallback from the live config
                int cs = cloud_get_config(s_cfg_buf[next]);  // parses into the idle buffer; renderer never sees it half-written
                if (cs == 200 && s_cfg_buf[next]->valid) {
                    prefetch_and_evict_assets(s_cfg_buf[next]);  // fill/prune flash cache before render
                    ui_set_config(s_cfg_buf[next]);     // publish under the LVGL lock (pointer swap)
                    time_sync_set_tz(s_cfg_buf[next]->clock_timezone);  // POSIX TZ from payload
                    ui_set_countdown(s_cfg_buf[next]->qr_timeout_seconds);  // keep duration current for any receipt path
                    s_cfg_live = next;
                    ui_render_state(s_state);           // re-render current screen with new config (cache hits)
                }
            }
            backoff = POLL_BACKOFF_MIN;
            idle_wait_or_qr_expiry(POLL_IDLE_MS);
        } else {
            ESP_LOGW(TAG, "poll failed (status=%d), backoff=%dms", status, backoff);
            ui_set_online(false);
            vTaskDelay(pdMS_TO_TICKS(backoff));
            backoff = backoff * 2 > POLL_BACKOFF_MAX ? POLL_BACKOFF_MAX : backoff * 2;
        }
    }
}

void app_state_run(void)
{
    // Allocate both config buffers in PSRAM (each ~67KB; too big for internal .bss).
    s_cfg_buf[0] = heap_caps_calloc(1, sizeof(device_config_t), MALLOC_CAP_SPIRAM);
    s_cfg_buf[1] = heap_caps_calloc(1, sizeof(device_config_t), MALLOC_CAP_SPIRAM);
    if (!s_cfg_buf[0] || !s_cfg_buf[1]) {
        ESP_LOGE(TAG, "config buffer PSRAM alloc failed; running without live config");
        return;   // boot splash/cached config from app_main stays on screen; no poll task
    }
    cloud_config_load_cached(s_cfg_buf[s_cfg_live]);   // seed ETag so the first fetch can 304
    ui_set_config(s_cfg_buf[s_cfg_live]);
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, NULL);
}
