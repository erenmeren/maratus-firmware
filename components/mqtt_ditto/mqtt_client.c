#include "mqtt_client_ditto.h"
#include "mqtt_topics.h"
#include "mqtt_client.h"          // esp-mqtt
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "appcfg.h"
#include <string.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static QueueHandle_t s_cmd_q;                 // dev_command_t, depth 4
static void (*s_wake_cb)(void);
static char s_device_id[64];                  // = username/client_id from config
static char s_host[128];                      // remember to detect config changes

static void on_cmd_message(const char *data, int len)
{
    cJSON *cmd = cJSON_ParseWithLength(data, len);
    if (!cmd) { ESP_LOGW(TAG, "bad cmd json"); return; }
    dev_command_t dc;
    if (command_parse_into(cmd, &dc)) {
        if (xQueueSend(s_cmd_q, &dc, 0) == pdTRUE) { ESP_LOGI(TAG, "rx+enqueue %s", dc.command_id); if (s_wake_cb) s_wake_cb(); }
        else ESP_LOGW(TAG, "cmd queue full, dropping %s", dc.command_id);
    }
    cJSON_Delete(cmd);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        char topic[80];
        mqtt_topic_cmd(topic, sizeof(topic), s_device_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);   // QoS 1, re-sub each connect
        ESP_LOGI(TAG, "connected, subscribed %s", topic);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
        // Only the cmd topic is subscribed, so any data is a command.
        on_cmd_message(e->data, e->data_len);
        break;
    default: break;
    }
}

bool mqtt_start(const device_config_t *cfg)
{
    if (!cfg || !cfg->mqtt.enabled) return false;
    const char *key = appcfg_device_key();
    if (!key || !key[0]) { ESP_LOGW(TAG, "no device key; MQTT not started"); return false; }

    // Already running for the same broker/identity → no-op.
    if (s_client && strcmp(s_host, cfg->mqtt.host) == 0 &&
        strcmp(s_device_id, cfg->mqtt.username) == 0) return true;
    if (s_client) mqtt_stop();

    snprintf(s_device_id, sizeof(s_device_id), "%s", cfg->mqtt.username);
    snprintf(s_host, sizeof(s_host), "%s", cfg->mqtt.host);
    if (!s_cmd_q) s_cmd_q = xQueueCreate(4, sizeof(dev_command_t));

    ESP_LOGI(TAG, "starting; free internal DRAM before=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_mqtt_client_config_t c = {
        .broker.address.hostname = cfg->mqtt.host,
        .broker.address.port = cfg->mqtt.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = cfg->mqtt.client_id,
        .credentials.username = cfg->mqtt.username,
        .credentials.authentication.password = key,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
        .buffer.size = 2048,
        .buffer.out_size = 512,
    };
    s_client = esp_mqtt_client_init(&c);
    if (!s_client) { ESP_LOGE(TAG, "init failed"); return false; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "started; free internal DRAM after=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

void mqtt_stop(void)
{
    if (!s_client) return;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
    s_host[0] = '\0'; s_device_id[0] = '\0';
}

bool mqtt_is_connected(void) { return s_connected; }

bool mqtt_poll_command(dev_command_t *out)
{
    if (!s_cmd_q) return false;
    return xQueueReceive(s_cmd_q, out, 0) == pdTRUE;
}

void mqtt_set_wake_cb(void (*cb)(void)) { s_wake_cb = cb; }

int mqtt_ack(const char *command_id, bool ok)
{
    if (!s_client || !s_connected) return -1;
    char topic[80];
    mqtt_topic_ack(topic, sizeof(topic), s_device_id);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "commandId", command_id);
    cJSON_AddBoolToObject(j, "ok", ok);
    char *body = cJSON_PrintUnformatted(j);
    if (!body) { cJSON_Delete(j); return -1; }
    int r = esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);   // QoS 1
    cJSON_free(body); cJSON_Delete(j);
    ESP_LOGI(TAG, "ack %s ok=%d msg_id=%d", command_id, ok, r);
    return r < 0 ? -1 : 0;
}

void mqtt_publish_hb(const char *version, int fonts, int afetch, int aimg)
{
    if (!s_client || !s_connected) return;
    char topic[80];
    mqtt_topic_hb(topic, sizeof(topic), s_device_id);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "version", version ? version : "");
    // Free internal DRAM (bytes) so the cloud can watch the concurrent-TLS
    // memory high-water mark remotely (this device's serial is unreliable).
    cJSON_AddNumberToObject(j, "heap",
        (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    // Font-cache slots in use: a jump here alongside a heap drop confirms the
    // one-time internal-DRAM step is font-face creation, not a leak.
    if (fonts >= 0) cJSON_AddNumberToObject(j, "fonts", (double)fonts);
    // Image-render diagnostics: afetch = last asset GET status, aimg = last image
    // render state (see mqtt_client_ditto.h). Pinpoints fetch vs decode/render.
    if (afetch > -999) cJSON_AddNumberToObject(j, "afetch", (double)afetch);
    if (aimg   > -999) cJSON_AddNumberToObject(j, "aimg",   (double)aimg);
    char *body = cJSON_PrintUnformatted(j);
    if (!body) { cJSON_Delete(j); return; }
    esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);
    cJSON_free(body); cJSON_Delete(j);
}
