#include "esp_log.h"
#include "bsp/esp-bsp.h"   // esp-bsp convention; if the demo's BSP uses a different
                            // header, change this include to match (see idf_component.yml).
#include "ui.h"
#include "net.h"
#include "cloud.h"

static const char *TAG = "ditto";

// ---------------------------------------------------------------------------
// BSP integration seam (reconcile with the Waveshare demo / docs/bsp-notes.md):
//   - bsp_display_start()        brings up the MIPI-DSI panel + starts the LVGL
//                                port task, returns the lv_display_t* (unused here).
//   - bsp_display_lock(0) / bsp_display_unlock()  guard LVGL calls from app tasks.
// If the demo wires esp_lcd + esp_lvgl_port directly instead of an esp-bsp BSP,
// substitute its init call here and swap the lock calls for lvgl_port_lock(0) /
// lvgl_port_unlock(). The ui component itself is pure LVGL and needs no changes.
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Ditto firmware boot (M2 skeleton)");

    // 1) Bring up display + LVGL via the board BSP.
    bsp_display_start();

    // 2) Build + show the idle screen under the LVGL lock.
    bsp_display_lock(0);
    ui_init();
    ui_show(UI_SCREEN_IDLE);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Idle screen shown");

    // 3) Connect to Wi-Fi via the C6 radio, then reflect status on the idle dot.
    net_start();
    bsp_display_lock(0);
    ui_set_online(net_is_connected());
    bsp_display_unlock();
    ESP_LOGI(TAG, "Wi-Fi connected=%d", net_is_connected());

    // 4) One boot poll of the cloud to prove the device-key seam. The continuous
    //    poll task + command dispatch land in Tasks 7-8.
    static char body[1024];
    int status = cloud_get_commands(body, sizeof(body));
    ESP_LOGI(TAG, "first poll status=%d", status);
}
