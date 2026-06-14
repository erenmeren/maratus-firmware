#include "esp_log.h"
#include "bsp/esp-bsp.h"   // bsp_display_start(); if the demo's BSP uses a different
                            // header/init, change this (see idf_component.yml).
#include "ui.h"
#include "net.h"
#include "state.h"

static const char *TAG = "ditto";

void app_main(void)
{
    ESP_LOGI(TAG, "Ditto firmware boot (M2 skeleton)");

    // 1) Bring up display + LVGL via the board BSP.
    bsp_display_start();

    // 2) Build + show the idle screen (ui takes the LVGL lock internally).
    ui_init();
    ui_show(UI_SCREEN_IDLE);
    ESP_LOGI(TAG, "Idle screen shown");

    // 3) Connect to Wi-Fi via the C6 radio.
    net_start();
    ESP_LOGI(TAG, "Wi-Fi connected=%d", net_is_connected());

    // 4) Start the continuous poll loop + device state machine.
    app_state_run();
    ESP_LOGI(TAG, "entered IDLE; polling");
}
