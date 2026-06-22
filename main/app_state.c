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
#include "ota.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "sleep_policy.h"
#include "settings_pin.h"
#include "esp_netif.h"
#include "wifi_setup.h"

static const char *TAG = "state";
// Written by poll_task and by render_job (the ESC/POS task) via app_state_set_state;
// volatile for cross-task visibility. A single enum word — read/write is atomic.
static volatile dev_state_t s_state = DEV_BOOT;
// Double-buffered config (parse into the idle buffer, publish under the LVGL lock).
// Each device_config_t is ~67KB, so both live in PSRAM (CPU-read only, no DMA) to
// keep internal DRAM free for Wi-Fi/SDIO/TLS buffers — allocated in app_state_run().
static device_config_t *s_cfg_buf[2];
static int s_cfg_live = 0;             // index of the buffer the renderer currently points at
static volatile bool s_config_dirty = true;   // fetch once after first connect

static volatile bool s_display_asleep = false;
static volatile uint32_t s_last_activity_ms = 0;
static volatile uint32_t s_wake_guard_until = 0;   // ignore taps until this time (swallow the wake touch)

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// Apply backlight brightness (0..100 duty). No-op while asleep — Task 4's
// sleep/wake owns the backlight then. Clamps defensively.
static void display_apply_brightness(int duty) {
    if (s_display_asleep) return;
    if (duty < 10)  duty = 10;
    if (duty > 100) duty = 100;
    bsp_display_brightness_set(duty);
}

static void display_sleep(void) {
    bsp_display_backlight_off();
    s_display_asleep = true;
}

static void display_wake(int duty) {
    s_display_asleep = false;            // clear first so display_apply_brightness isn't a no-op
    bsp_display_backlight_on();          // ensure the backlight controller is on
    display_apply_brightness(duty);      // configured duty is the final write (wins over backlight_on's 100)
    uint32_t t = now_ms();
    s_last_activity_ms = t;
    s_wake_guard_until = t + 800; // the touch that woke us must not also trigger ingest
    ui_consume_tap();                    // swallow any tap already latched by the wake touch
}

void app_state_request_config(void) { s_config_dirty = true; }

void app_state_set_state(dev_state_t st) {
    // A print job arriving while asleep must light the screen so the QR is visible.
    if (s_display_asleep && (st == DEV_PROCESSING || st == DEV_QR))
        display_wake(s_cfg_buf[s_cfg_live]->device.brightness);
    s_state = st;
}

#define POLL_IDLE_MS     12000
#define POLL_BACKOFF_MIN 2000
#define POLL_BACKOFF_MAX 60000
#define OTA_CHECK_EVERY  50
static int s_ota_poll_ctr = 0;

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

// Build a human-readable settings info string (firmware version, Wi-Fi SSID,
// signal bars, IP address, online status). Device name/ID are not stored on-device
// and are intentionally omitted.
static void build_settings_info(char *out, int cap) {
    char ssid[33] = "\xe2\x80\x94";  // em-dash (UTF-8) when not connected
    int bars = 0;
    char ip[16] = "\xe2\x80\x94";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(ssid, sizeof(ssid), "%s", (const char *)ap.ssid);
        bars = rssi_to_level(ap.rssi);
    }
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ipi;
    if (nif && esp_netif_get_ip_info(nif, &ipi) == ESP_OK && ipi.ip.addr != 0)
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ipi.ip));
    snprintf(out, cap,
             "Firmware: %s\nWi-Fi: %s (%d/4)\nIP: %s\nStatus: %s",
             CONFIG_DITTO_FW_VERSION, ssid, bars, ip,
             net_is_connected() ? "Online" : "Offline");
}

// Long-press → PIN gate (if configured) → settings menu.
// Blocks until the user closes the menu, runs a test print, or reboots.
static void run_settings_flow(void) {
    const device_config_t *cfg = s_cfg_buf[s_cfg_live];
    ui_settings_action_t act;

    // PIN gate — skipped when no hash is configured.
    if (cfg->device.settings_password_hash[0]) {
        ui_settings_show_pin();
        for (;;) {
            char pin[16];
            if (ui_settings_consume_pin(pin, sizeof(pin))) {
                if (settings_pin_verify(pin,
                                        cfg->device.settings_password_hash,
                                        cfg->device.settings_password_salt)) {
                    // Correct PIN — drain the stale Cancel action the PIN screen
                    // latches when the user taps Submit (not Cancel), then continue.
                    ui_settings_consume_action(&act);
                    break;
                }
                ui_settings_pin_set_error(true);
            }
            // Let Cancel close the PIN screen without opening the menu.
            if (ui_settings_consume_action(&act) && act == UI_SET_CLOSE) {
                ui_render_state(s_state);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    // Build info and show menu.
    char info[160];
    build_settings_info(info, sizeof(info));
    ui_settings_set_info(info);
    ui_settings_show_menu();

    for (;;) {
        if (ui_settings_consume_action(&act)) {
            switch (act) {
                case UI_SET_WIFI:
                    wifi_setup_run();
                    // Refresh info after Wi-Fi changes and re-show menu.
                    build_settings_info(info, sizeof(info));
                    ui_settings_set_info(info);
                    ui_settings_show_menu();
                    break;
                case UI_SET_TEST_PRINT:
                    run_test_ingest();
                    return;
                case UI_SET_REBOOT:
                    esp_restart();
                    break;
                case UI_SET_CLOSE:
                    ui_render_state(s_state);
                    return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Wait up to total_ms, but break early (~500ms granularity) to auto-return from
// the receipt screen when its countdown expires. Avoids extra cloud polls.
static void idle_wait_or_qr_expiry(int total_ms) {
    const int slice = 500;
    // Check at 0, slice, 2*slice, ... and once more at total_ms (delay between
    // checks, but not after the last). The trailing check catches an expiry that
    // lands in the final slice, instead of waiting a whole cloud poll round-trip.
    for (int waited = 0; waited <= total_ms; waited += slice) {
        if (ui_consume_countdown_expired()) {
            s_state = DEV_IDLE;
            ui_render_state(DEV_IDLE);
            return;
        }
        const device_config_t *cfg = s_cfg_buf[s_cfg_live];
        bool active = ui_consume_activity();
        if (active) s_last_activity_ms = now_ms();
        // A receipt that arrived while asleep (or a sleep that raced the wake)
        // must light the screen so the QR is visible.
        if (s_display_asleep && (s_state == DEV_PROCESSING || s_state == DEV_QR)) {
            display_wake(cfg->device.brightness);
        }
        if (cfg->device.sleep.enabled) {
            if (!s_display_asleep &&
                should_sleep(now_ms(), s_last_activity_ms,
                             cfg->device.sleep.timeout_seconds, true, s_state)) {
                display_sleep();
            } else if (s_display_asleep && active) {
                display_wake(cfg->device.brightness);
            }
        } else if (s_display_asleep) {
            display_wake(cfg->device.brightness);   // sleep turned off -> ensure the panel is on
        }
        if (waited < total_ms) vTaskDelay(pdMS_TO_TICKS(slice));
    }
}

static void poll_task(void *arg)
{
    static char body[1024];
    int backoff = POLL_BACKOFF_MIN;
    s_state = DEV_IDLE;
    s_last_activity_ms = now_ms();

    for (;;) {
        if (ui_consume_tap()) {
            // A tap that merely woke the screen must not also trigger ingest.
            if (now_ms() >= s_wake_guard_until && net_is_connected()) run_test_ingest();
            continue;
        }
        if (ui_consume_sheet_open()) {
            ui_sheet_close();      // remove the cover so the PIN/menu screen is visible
            run_settings_flow();
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
                    display_apply_brightness(s_cfg_buf[next]->device.brightness);
                    time_sync_set_tz(s_cfg_buf[next]->clock_timezone);  // POSIX TZ from payload
                    ui_set_countdown(s_cfg_buf[next]->qr_timeout_seconds);  // keep duration current for any receipt path
                    s_cfg_live = next;
                    ui_render_state(s_state);           // re-render current screen with new config (cache hits)
                }
            }
            backoff = POLL_BACKOFF_MIN;
            ota_mark_valid_if_pending();   // healthy check-in → confirm any freshly-OTA'd image
            if (++s_ota_poll_ctr >= OTA_CHECK_EVERY) {
                s_ota_poll_ctr = 0;
                ota_check_and_update(false);   // may reboot
            }
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
    if (s_cfg_buf[s_cfg_live]->valid)
        display_apply_brightness(s_cfg_buf[s_cfg_live]->device.brightness);
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, NULL);
}
