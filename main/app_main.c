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

static const char *TAG = "ditto";

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
    static device_config_t g_cfg;
    cloud_config_load_cached(&g_cfg);
    ui_set_config(&g_cfg);
    ui_render_state(g_cfg.valid ? DEV_IDLE : DEV_BOOT);
    ESP_LOGI(TAG, "initial screen shown (valid_cfg=%d)", g_cfg.valid);

    // 3) Connect to Wi-Fi via the C6 (SDIO/esp-hosted).
    net_start();
    ESP_LOGI(TAG, "Wi-Fi connected=%d", net_is_connected());

    // 4) Continuous poll loop + device state machine.
    app_state_run();
    cloud_set_config_changed_cb(app_state_request_config);

    // 5) ESC/POS print-job listener on TCP:9100.
    escpos_server_start(render_job_handle);
    ESP_LOGI(TAG, "print server up on :9100");
}
