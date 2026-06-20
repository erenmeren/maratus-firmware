#include "esp_log.h"
#include "bsp/esp-bsp.h"   // Waveshare board BSP (waveshare/esp32_p4_wifi6_touch_lcd_4b)
#include "bsp/display.h"
#include "ui.h"
#include "net.h"
#include "state.h"
#include "escpos_server.h"
#include "render_job.h"
#include "render.h"
#include "appcfg.h"
#include "nvs_flash.h"
#include "cloud.h"
#include "device_config.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_system.h"          // esp_restart
#include "provisioning.h"        // provisioning_default_setup_config
#include "wifi_setup.h"          // wifi_setup_run (first-boot on-screen Wi-Fi)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ditto";

// Unauthenticated claim poll: while the device has no NVS device key, show the
// setup screen and poll GET /api/device/claim with our pairing code. When the
// operator pairs it in the dashboard the server returns a deviceKey; we persist
// it and reboot into the normal (claimed) boot path.
static void provisioning_task(void *arg) {
    (void)arg;
    const char *code = appcfg_pairing_code();
    char key[80];
    for (;;) {
        if (net_is_connected() && cloud_claim_poll(code, key, sizeof(key))) {
            ESP_LOGI(TAG, "claimed; storing device key + rebooting");
            appcfg_store_device_key(key);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Mount the on-flash asset cache (the `assets` LittleFS partition) at /assets.
// Failure is non-fatal: the rest of boot continues, asset_cache_get just misses.
static void assets_fs_mount(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/assets",
        .partition_label = "assets",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "assets LittleFS mount failed: %s", esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    if (esp_littlefs_info("assets", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "assets LittleFS mounted at /assets (%u/%u bytes used)",
                 (unsigned)used, (unsigned)total);
    } else {
        ESP_LOGI(TAG, "assets LittleFS mounted at /assets");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Ditto firmware boot (M2 skeleton)");

    // 0) Initialize NVS (device config cache lives here).
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    // 1) Bring up display + LVGL via the Waveshare board BSP (matches the demo).
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = false },
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_backlight_on();
    bsp_display_brightness_set(100);   // ensure the panel isn't dark (PWM duty)
    render_set_roll_width(appcfg_roll_width());

    // 2) Build + show the idle screen (ui takes the LVGL lock internally).
    ui_init();

    // 2b) Render any cached config immediately (before Wi-Fi); else built-in splash.
    // device_config_t is ~67KB; keep it out of internal .bss (PSRAM, CPU-read only, no DMA).
    static device_config_t *g_cfg = NULL;
    g_cfg = heap_caps_calloc(1, sizeof(*g_cfg), MALLOC_CAP_SPIRAM);
    if (g_cfg) {
        cloud_config_load_cached(g_cfg);
        ui_set_config(g_cfg);
        ui_render_state(g_cfg->valid ? DEV_IDLE : DEV_BOOT);
        ESP_LOGI(TAG, "initial screen shown (valid_cfg=%d)", g_cfg->valid);
    } else {
        ESP_LOGE(TAG, "g_cfg PSRAM alloc failed; showing splash");
        ui_render_state(DEV_BOOT);
    }

    // 3) Wi-Fi: connect with stored/Kconfig creds, else run on-screen setup (first boot).
    net_init();
    if (appcfg_has_wifi_creds() ||
        (appcfg_wifi_ssid()[0] && strcmp(appcfg_wifi_ssid(), "changeme") != 0)) {
        net_connect(appcfg_wifi_ssid(), appcfg_wifi_password());
    } else {
        wifi_setup_run();
    }
    ESP_LOGI(TAG, "Wi-Fi connected=%d", net_is_connected());

    // 3b) Mount the on-flash asset-cache filesystem (non-fatal on failure).
    assets_fs_mount();

    // 3c) Unclaimed device: show the setup screen + run the claim-poll task, then
    // stop here (no print server / state machine until the device has a key).
    if (!appcfg_has_device_key()) {
        ESP_LOGI(TAG, "no device key; entering provisioning mode");
        ESP_LOGI(TAG, "pairing code: %s", appcfg_pairing_code());
        // ~67KB config — keep it in PSRAM (not internal .bss), like g_cfg above.
        // Lives until the post-claim reboot; never freed (intentional).
        device_config_t *setup_cfg = heap_caps_calloc(1, sizeof(*setup_cfg), MALLOC_CAP_SPIRAM);
        if (setup_cfg) {
            provisioning_default_setup_config(setup_cfg);
            ui_set_config(setup_cfg);   // NULL → ui falls back to the splash; claim still proceeds
        }
        ui_set_pairing_code(appcfg_pairing_code());
        ui_render_state(DEV_SETUP);
        xTaskCreate(provisioning_task, "ditto_provision", 8192, NULL, 5, NULL);
        return;
    }

    // 4) Continuous poll loop + device state machine.
    app_state_run();
    cloud_set_config_changed_cb(app_state_request_config);

    // 5) ESC/POS print-job listener on TCP:9100.
    escpos_server_start(render_job_handle);
    ESP_LOGI(TAG, "print server up on :9100");
}
