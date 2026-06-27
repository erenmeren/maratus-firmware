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
#include "esp_mac.h"
#include "ota.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "sleep_policy.h"
#include "settings_pin.h"
#include "esp_netif.h"
#include "wifi_setup.h"
#include "overrides.h"
#include "brightness.h"
#include "nvs.h"

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
// Set true (escpos task) when a document arrives while run_settings_flow is mid-flow;
// the flow polls this and returns within one 50 ms slice. volatile for cross-task read.
static volatile bool s_settings_abort = false;

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// Device-local settings (brightness/sleep) governed by a single "use device
// settings" switch: when use_device is 0 (fresh NVS) the effective values follow
// the admin's pushed policy exactly; when 1 the device-local values win.
// Persisted in NVS namespace "ditto". Init to a fresh struct at declaration so
// the escpos task's app_state_set_state wake path can't read a zero-initialised
// struct in the boot window before overrides_load() runs (use_device=0 means it
// follows cloud regardless).
static device_settings_t g_dev = { .use_device = 0, .brightness = -1, .sleep = -1 };

// Encode cloud sleep the same way a device value is: 0 = off, >0 = timeout secs.
static int cloud_sleep_encoded(const device_config_t *c) {
    return c->device.sleep.enabled ? c->device.sleep.timeout_seconds : 0;
}

// Load device settings from NVS. Absent keys / open failure leave the defaults
// (use_device=0, values -1) — so a fresh/erased NVS follows cloud exactly.
static void overrides_load(void) {
    devset_init(&g_dev);
    nvs_handle_t h;
    if (nvs_open("ditto", NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, "dev_use", &v) == ESP_OK) g_dev.use_device = v;
    if (nvs_get_i32(h, "dev_br",  &v) == ESP_OK) g_dev.brightness = v;
    if (nvs_get_i32(h, "dev_sl",  &v) == ESP_OK) g_dev.sleep      = v;
    nvs_close(h);
}

// Persist the device-settings ints to NVS.
static void overrides_save(void) {
    nvs_handle_t h;
    if (nvs_open("ditto", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "dev_use", g_dev.use_device);
    nvs_set_i32(h, "dev_br",  g_dev.brightness);
    nvs_set_i32(h, "dev_sl",  g_dev.sleep);
    nvs_commit(h);
    nvs_close(h);
}

// Apply backlight brightness for a perceptual `level` (10..100). The level is
// mapped through a gamma curve to a PWM duty so the low/mid range isn't too dark
// (uniformly for device + admin brightness). No-op while asleep — Task 4's
// sleep/wake owns the backlight then.
static void display_apply_brightness(int level) {
    if (s_display_asleep) return;
    bsp_display_brightness_set(brightness_level_to_duty(level));
}

static void display_sleep(void) {
    bsp_display_backlight_off();
    s_display_asleep = true;
    ui_set_input_suppressed(true); // a swipe on a sleeping screen wakes only
}

static void display_wake(int level) {
    s_display_asleep = false;            // clear first so display_apply_brightness isn't a no-op
    bsp_display_backlight_on();          // ensure the backlight controller is on
    display_apply_brightness(level);     // perceptual level (gamma-curved inside) is the final write
    uint32_t t = now_ms();
    s_last_activity_ms = t;
    s_wake_guard_until = t + 800; // the touch that woke us must not also trigger ingest
    ui_set_input_suppressed(true); // stay suppressed through the wake-guard
    ui_consume_tap();                    // swallow any tap already latched by the wake touch
}

void app_state_request_config(void) { s_config_dirty = true; }

void app_state_set_state(dev_state_t st) {
    // A print job arriving while asleep must light the screen so the QR is visible.
    if (s_display_asleep && (st == DEV_PROCESSING || st == DEV_QR))
        display_wake(devset_eff_brightness(&g_dev, s_cfg_buf[s_cfg_live]->device.brightness));
    // A document arriving while Settings is open must show the live QR. Signal any
    // in-flight settings flow to abort; render_job already loads the QR screen, and
    // the aborting flow deletes its own settings screen. (called from the escpos task.)
    if (st == DEV_PROCESSING || st == DEV_QR) {
        s_settings_abort = true;
    }
    s_state = st;
}

#define POLL_IDLE_MS     12000
#define POLL_BACKOFF_MIN 2000
#define POLL_BACKOFF_MAX 60000
#define OTA_CHECK_EVERY  50
static int s_ota_poll_ctr = 0;

// M3: upload the bundled test PNG and show the returned document URL as a QR.
// Replaced by the live ESC/POS render path in M4.
static void run_test_ingest(void)
{
    ui_render_state(DEV_PROCESSING);
    s_state = DEV_PROCESSING;

    size_t len = 0;
    const uint8_t *png = assets_test_png(&len);

    static char url[256];
    int status = cloud_post_document(png, len, url, sizeof(url));
    if (status == 201 && url[0]) {
        ESP_LOGI(TAG, "document ready: %s", url);
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

// Collect the uploaded-image asset URLs a config references: every visible
// OBJ_ICON with icon_src==ICON_UPLOAD and every visible OBJ_IMAGE across all
// screens. urls[] alias into cfg (no copy). Returns the count (<= cap).
static int gather_asset_urls(const device_config_t *cfg, const char *urls[], int cap)
{
    int n = 0;
    for (int s = 0; s < SCREEN_COUNT && n < cap; s++) {
        const cfg_screen_t *sc = &cfg->screens[s];
        for (int i = 0; i < sc->n && n < cap; i++) {
            const cfg_object_t *o = &sc->objects[i];
            if (o->type == OBJ_ICON && o->icon_src == ICON_UPLOAD &&
                o->visible && o->icon_url[0]) {
                urls[n++] = o->icon_url;
            } else if (o->type == OBJ_IMAGE && o->visible && o->image_url[0] && n < cap) {
                urls[n++] = o->image_url;
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

// Build the About sub-screen content: one "label\tvalue\n" line per fact
// (firmware, IP, MAC, Wi-Fi, signal, Online, print listener). Rendered as a
// read-only card list by ui_settings_show_about.
static void build_about_info(char *out, size_t n) {
    char ssid[33] = "\xe2\x80\x94";  // em-dash when not connected
    char bars[8]  = "\xe2\x80\x94";
    char ip[16]   = "\xe2\x80\x94";
    char mac[18]  = "\xe2\x80\x94";

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(ssid, sizeof(ssid), "%s", (const char *)ap.ssid);
        snprintf(bars, sizeof(bars), "%d/4", rssi_to_level(ap.rssi));
    }
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ipi;
    if (nif && esp_netif_get_ip_info(nif, &ipi) == ESP_OK && ipi.ip.addr != 0)
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ipi.ip));

    uint8_t m[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, m) == ESP_OK ||
        esp_read_mac(m, ESP_MAC_WIFI_STA) == ESP_OK)
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 m[0], m[1], m[2], m[3], m[4], m[5]);

    snprintf(out, n,
             "Firmware\t%s\n"
             "IP\t%s\n"
             "MAC\t%s\n"
             "Wi-Fi\t%s\n"
             "Signal\t%s\n"
             "Online\t%s\n"
             "Print listener\tListening",
             CONFIG_DITTO_FW_VERSION, ip, mac, ssid, bars,
             net_is_connected() ? "Online" : "Offline");
}

// Return to the live idle/branding screen and free the settings screen.
// ui_render_state() loads a fresh branding screen and deletes ui.c's old s_active
// (the branding screen that was hidden under settings) — then scr is orphaned and
// safe to delete.
static void close_settings(lv_obj_t *scr) {
    ui_render_state(s_state);
    lv_obj_delete(scr);
    ui_settings_reset();
}

// Swipe-up → PIN gate (if configured) → settings menu, on a full-screen overlay
// that slides up. Blocks until the user closes the menu, runs a test print, or
// reboots — or until a document aborts the flow.
static void run_settings_flow(void) {
    const device_config_t *cfg = s_cfg_buf[s_cfg_live];
    ui_settings_action_t act;
    s_settings_abort = false;

    // Build the settings screen and slide it up. auto_del=false: the branding
    // screen it replaces is still owned by ui.c (s_active) — must NOT be deleted.
    lv_obj_t *scr = lv_obj_create(NULL);
    char info[160];

    if (cfg->device.settings_password_hash[0]) {
        ui_settings_show_pin(scr);
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
        for (;;) {
            if (s_settings_abort) { lv_obj_delete(scr); ui_settings_reset(); return; }
            char pin[16];
            if (ui_settings_consume_pin(pin, sizeof(pin))) {
                if (settings_pin_verify(pin, cfg->device.settings_password_hash,
                                        cfg->device.settings_password_salt)) {
                    ui_settings_consume_action(&act);   // drain the stale latch
                    break;
                }
                ui_settings_pin_set_error(true);
            }
            if (ui_settings_consume_action(&act) && act == UI_SET_CLOSE) {
                close_settings(scr); return;            // helper above
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    } else {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
    }

    // Menu — build into the SAME scr (ui_settings_show_menu deletes the old pin
    // child container and adds the menu; scr is already the active screen).
    build_settings_info(info, sizeof(info));
    ui_settings_set_info(info);
    ui_settings_show_menu(scr);

    for (;;) {
        if (s_settings_abort) { lv_obj_delete(scr); ui_settings_reset(); return; }
        if (ui_settings_consume_action(&act)) {
            switch (act) {
                case UI_SET_WIFI:
                    // wifi_setup_run() lv_screen_loads its own screen, replacing scr
                    // as the active screen (scr stays alive, hidden). After it returns,
                    // rebuild the menu into scr and slide scr back up.
                    wifi_setup_run();
                    build_settings_info(info, sizeof(info));
                    ui_settings_set_info(info);
                    ui_settings_show_menu(scr);
                    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
                    break;
                case UI_SET_ABOUT: {
                    // Read-only sub-screen: show device info, wait for Back,
                    // then rebuild the menu in place (same scr — no new load).
                    char about[256];
                    build_about_info(about, sizeof(about));
                    ui_settings_show_about(scr, about);
                    for (;;) {
                        if (s_settings_abort) {
                            lv_obj_delete(scr); ui_settings_reset(); return;
                        }
                        if (ui_settings_consume_action(&act) && act == UI_SET_BACK)
                            break;
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    ui_settings_show_menu(scr);
                    break;
                }
                case UI_SET_TEST_PRINT:
                    close_settings(scr);
                    run_test_ingest();
                    return;
                case UI_SET_REBOOT:
                    esp_restart();
                    break;
                case UI_SET_CLOSE:
                    close_settings(scr);
                    return;
                case UI_SET_DEVICE: {
                    // Interactive sub-screen: a single "Use device settings"
                    // switch picks the source (admin vs device) for BOTH
                    // brightness + sleep. When on, the brightness slider (live
                    // preview, persist on release) + sleep chips are editable;
                    // when off they're disabled and follow the admin policy.
                    int eff_br = devset_eff_brightness(&g_dev, cfg->device.brightness);
                    int eff_sl = devset_eff_sleep(&g_dev, cloud_sleep_encoded(cfg));
                    ui_settings_show_device(scr, g_dev.use_device, eff_br, eff_sl);
                    for (;;) {
                        if (s_settings_abort) { lv_obj_delete(scr); ui_settings_reset(); return; }
                        bool on;
                        if (ui_settings_consume_use_device(&on)) {
                            devset_set_use_device(&g_dev, on, cfg->device.brightness, cloud_sleep_encoded(cfg));
                            overrides_save();
                            display_apply_brightness(devset_eff_brightness(&g_dev, cfg->device.brightness));
                            s_last_activity_ms = now_ms();
                            ui_settings_show_device(scr, g_dev.use_device,
                                devset_eff_brightness(&g_dev, cfg->device.brightness),
                                devset_eff_sleep(&g_dev, cloud_sleep_encoded(cfg)));   // re-render to enable/disable controls
                        }
                        int v;
                        if (g_dev.use_device && ui_settings_consume_brightness(&v)) display_apply_brightness(v);     // live
                        if (g_dev.use_device && ui_settings_consume_brightness_commit(&v)) { devset_set_brightness(&g_dev, v); overrides_save(); }
                        int e;
                        if (g_dev.use_device && ui_settings_consume_sleep(&e)) { devset_set_sleep(&g_dev, e); overrides_save(); s_last_activity_ms = now_ms(); }
                        if (ui_settings_consume_action(&act) && act == UI_SET_BACK)
                            break;
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    ui_settings_show_menu(scr);
                    break;
                }
                case UI_SET_BACK:     // only meaningful inside a sub-screen loop.
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Wait up to total_ms, but break early (~500ms granularity) to auto-return from
// the document screen when its countdown expires. Avoids extra cloud polls.
static void idle_wait_or_qr_expiry(int total_ms) {
    const int slice = 500;
    // Check at 0, slice, 2*slice, ... and once more at total_ms (delay between
    // checks, but not after the last). The trailing check catches an expiry that
    // lands in the final slice, instead of waiting a whole cloud poll round-trip.
    for (int waited = 0; waited <= total_ms; waited += slice) {
        // A swipe-up just happened — bail so poll_task runs the settings flow
        // within one slice instead of after a whole poll cycle (~12s). The flag
        // stays set; the main loop consumes it via ui_consume_swipeup().
        if (ui_swipeup_pending()) return;
        if (ui_consume_countdown_expired()) {
            s_state = DEV_IDLE;
            ui_render_state(DEV_IDLE);
            return;
        }
        const device_config_t *cfg = s_cfg_buf[s_cfg_live];
        // Keep input suppressed while asleep or within the wake-guard; release it
        // once the screen is awake and past the guard (idempotent every slice).
        ui_set_input_suppressed(s_display_asleep || now_ms() < s_wake_guard_until);
        bool active = ui_consume_activity();
        if (active) s_last_activity_ms = now_ms();
        // A document that arrived while asleep (or a sleep that raced the wake)
        // must light the screen so the QR is visible.
        if (s_display_asleep && (s_state == DEV_PROCESSING || s_state == DEV_QR)) {
            display_wake(devset_eff_brightness(&g_dev, cfg->device.brightness));
        }
        // Effective sleep: device value (0=off, >0=timeout) if on, else cloud.
        int es = devset_eff_sleep(&g_dev, cloud_sleep_encoded(cfg));
        bool sleep_en = es > 0;
        int  sleep_to = es > 0 ? es : 0;
        int  wake_br  = devset_eff_brightness(&g_dev, cfg->device.brightness);
        if (sleep_en) {
            if (!s_display_asleep &&
                should_sleep(now_ms(), s_last_activity_ms,
                             sleep_to, true, s_state)) {
                display_sleep();
            } else if (s_display_asleep && active) {
                display_wake(wake_br);
            }
        } else if (s_display_asleep) {
            display_wake(wake_br);   // sleep off -> ensure the panel is on
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
        // Release input suppression once awake and past the wake-guard (idempotent).
        ui_set_input_suppressed(s_display_asleep || now_ms() < s_wake_guard_until);
        if (ui_consume_tap()) {
            // A tap that merely woke the screen must not also trigger ingest.
            if (now_ms() >= s_wake_guard_until && net_is_connected()) run_test_ingest();
            continue;
        }
        if (ui_consume_swipeup()) {
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
                    // Apply the EFFECTIVE brightness (device value if "use device
                    // settings" is on, else the admin's pushed brightness).
                    display_apply_brightness(devset_eff_brightness(&g_dev, s_cfg_buf[next]->device.brightness));
                    time_sync_set_tz(s_cfg_buf[next]->clock_timezone);  // POSIX TZ from payload
                    ui_set_countdown(s_cfg_buf[next]->qr_timeout_seconds);  // keep duration current for any document path
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
    overrides_load();   // restore device settings (use_device=0 == follow cloud)
    cloud_config_load_cached(s_cfg_buf[s_cfg_live]);   // seed ETag so the first fetch can 304
    ui_set_config(s_cfg_buf[s_cfg_live]);
    if (s_cfg_buf[s_cfg_live]->valid)
        display_apply_brightness(devset_eff_brightness(&g_dev, s_cfg_buf[s_cfg_live]->device.brightness));
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, NULL);
}
