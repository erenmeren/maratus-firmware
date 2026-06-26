#include "commands.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "cloud.h"
#include "ui.h"
#include "command_parse.h"

static const char *TAG = "cmd";

static void do_identify(void)
{
    // Visible acknowledgement: flash the idle status dot a few times.
    // ui_set_online takes the LVGL lock internally, so no locking here.
    for (int i = 0; i < 6; i++) {
        ui_set_online(i % 2 == 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void commands_handle_body(const char *json_body)
{
    if (!json_body || !json_body[0]) return;
    cJSON *root = cJSON_Parse(json_body);
    if (!root) { ESP_LOGW(TAG, "bad JSON"); return; }

    cJSON *cmds = cJSON_GetObjectItem(root, "commands");
    int n = cJSON_IsArray(cmds) ? cJSON_GetArraySize(cmds) : 0;
    for (int i = 0; i < n; i++) {
        cJSON *cmd = cJSON_GetArrayItem(cmds, i);
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "id"));
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "type"));
        if (!id || !type) continue;
        ESP_LOGI(TAG, "command %s type=%s", id, type);

        if (strcmp(type, "identify") == 0) {
            do_identify();
            cloud_ack_command(id, true);
        } else if (strcmp(type, "reboot") == 0) {
            cloud_ack_command(id, true);   // ack BEFORE restarting
            ESP_LOGI(TAG, "rebooting");
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();                 // does not return
        } else if (strcmp(type, "config-changed") == 0 || strcmp(type, "refresh") == 0) {
            void (*cb)(void) = cloud_config_changed_handler();
            if (cb) cb();
            cloud_ack_command(id, true);
        } else if (strcmp(type, "firmware-update") == 0) {
            cloud_ack_command(id, true);              // ack BEFORE OTA (which reboots)
            void (*fcb)(void) = cloud_firmware_update_handler();
            if (fcb) fcb();
        } else if (strcmp(type, "trigger") == 0) {
            const char *url = command_trigger_show_qr_url(cmd);
            void (*tcb)(const char *) = cloud_trigger_handler();
            if (url && tcb) {
                tcb(url);
                cloud_ack_command(id, true);   // displayed -> cloud settles (charged)
            } else {
                cloud_ack_command(id, false);  // bad/missing url or no handler -> cloud releases
            }
        } else {
            cloud_ack_command(id, true);              // unknown: ack + ignore
        }
    }
    cJSON_Delete(root);
}
