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

void dispatch_command(const dev_command_t *cmd, ack_fn_t ack)
{
    const char *id = cmd->command_id;
    ESP_LOGI(TAG, "command %s type=%s", id, cmd->type);

    if (strcmp(cmd->type, "identify") == 0) {
        do_identify();
        ack(id, true);
    } else if (strcmp(cmd->type, "reboot") == 0) {
        ack(id, true);   // ack BEFORE restarting
        ESP_LOGI(TAG, "rebooting");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();   // does not return
    } else if (strcmp(cmd->type, "config-changed") == 0 || strcmp(cmd->type, "refresh") == 0) {
        void (*cb)(void) = cloud_config_changed_handler();
        if (cb) cb();
        ack(id, true);
    } else if (strcmp(cmd->type, "firmware-update") == 0) {
        ack(id, true);   // ack BEFORE OTA (which reboots)
        void (*fcb)(void) = cloud_firmware_update_handler();
        if (fcb) fcb();
    } else if (strcmp(cmd->type, "trigger") == 0) {
        void (*tcb)(const char *) = cloud_trigger_handler();
        if (cmd->url[0] && tcb) {
            tcb(cmd->url);
            ack(id, true);    // displayed -> cloud settles (charged)
        } else {
            ack(id, false);   // bad/missing url or no handler -> cloud releases
        }
    } else {
        ack(id, true);   // unknown: ack + ignore
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
        dev_command_t c;
        if (command_parse_into(cJSON_GetArrayItem(cmds, i), &c))
            dispatch_command(&c, cloud_ack_command);
    }
    cJSON_Delete(root);
}
