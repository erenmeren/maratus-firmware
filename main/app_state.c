#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cloud.h"
#include "commands.h"
#include "net.h"
#include "ui.h"

static const char *TAG = "state";
static dev_state_t s_state = DEV_BOOT;

#define POLL_IDLE_MS     12000
#define POLL_BACKOFF_MIN 2000
#define POLL_BACKOFF_MAX 60000

static void poll_task(void *arg)
{
    static char body[1024];
    int backoff = POLL_BACKOFF_MIN;
    s_state = DEV_IDLE;

    for (;;) {
        if (!net_is_connected()) {
            ui_set_online(false);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        int status = cloud_get_commands(body, sizeof(body));
        if (status == 200) {
            ui_set_online(true);
            commands_handle_body(body);   // may esp_restart() on reboot
            backoff = POLL_BACKOFF_MIN;
            vTaskDelay(pdMS_TO_TICKS(POLL_IDLE_MS));
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
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, NULL);
}
