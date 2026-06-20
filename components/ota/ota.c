#include "ota.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "cloud.h"
#include "appcfg.h"
#include "ota_manifest.h"

static const char *TAG = "ota";

void ota_check_and_update(bool forced)
{
    fw_manifest_t m;
    if (!cloud_get_firmware(&m)) return;
    if (!forced && !ota_should_update(appcfg_fw_version(), m.version)) return;
    ESP_LOGI(TAG, "OTA %s -> %s (%d bytes)", appcfg_fw_version(), m.version, m.size);

    esp_http_client_config_t http = {
        .url = m.url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http };
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA ok, rebooting into %s", m.version);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s (staying on %s)", esp_err_to_name(err), appcfg_fw_version());
    }
}

void ota_mark_valid_if_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "marked OTA image valid (rollback cancelled)");
        }
    }
}
