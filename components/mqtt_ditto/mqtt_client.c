#include "mqtt_client_ditto.h"
#include "mqtt_topics.h"
#include "mqtt_client.h"          // esp-mqtt
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "appcfg.h"
#include "cfg_parse.h"
#include "cmd_envelope.h"
#include "cloud.h"
#include <string.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static QueueHandle_t s_cmd_q;                 // dev_command_t, depth 4
static void (*s_wake_cb)(void);
static char s_device_id[64];                  // = username/client_id from config
static char s_host[128];                      // remember to detect config changes

// Inbound config payloads are ~5-20KB while esp-mqtt's receive buffer is 1KB,
// so MQTT_EVENT_DATA arrives fragmented. Reassemble into PSRAM (internal DRAM
// is the scarce resource) and let poll_task parse straight out of this slot
// under the mutex — the parse is milliseconds and inbound configs are rare.
#define STAGE_CAP (32 * 1024)
static char *s_stage;                  // PSRAM, lazily allocated
static int s_stage_len;
static volatile bool s_stage_ready;    // a config-changed payload is staged
static volatile bool s_ota_ready;      // a firmware-update manifest is staged
static SemaphoreHandle_t s_stage_mux;

static bool stage_ensure(void)
{
    if (!s_stage_mux) s_stage_mux = xSemaphoreCreateMutex();
    if (!s_stage) s_stage = heap_caps_malloc(STAGE_CAP, MALLOC_CAP_SPIRAM);
    return s_stage && s_stage_mux;
}

// Which of the two staged-payload kinds (if any) a just-parsed command carries.
typedef enum { STAGE_NONE, STAGE_CONFIG, STAGE_OTA } stage_kind_t;

// Parses a complete command envelope (always the staging slot's bytes — see
// on_data) and enqueues a dev_command_t. Returns which staged-payload kind
// this command is (config-changed or firmware-update), so the caller can mark
// the already-staged payload pending for poll_task. Does not touch
// s_stage_ready/s_ota_ready itself: every touch of those flags happens under
// s_stage_mux, in on_data, so a single writer task (esp-mqtt) can't race a
// push it hasn't finished flagging yet against one it's already started
// overwriting.
static stage_kind_t on_cmd_message(const char *data, int len)
{
    cJSON *cmd = cJSON_ParseWithLength(data, len);
    if (!cmd) { ESP_LOGW(TAG, "bad cmd json"); return STAGE_NONE; }
    dev_command_t dc;
    stage_kind_t kind = STAGE_NONE;
    if (command_parse_into(cmd, &dc)) {
        if (xQueueSend(s_cmd_q, &dc, 0) == pdTRUE) { ESP_LOGI(TAG, "rx+enqueue %s", dc.command_id); if (s_wake_cb) s_wake_cb(); }
        else ESP_LOGW(TAG, "cmd queue full, dropping %s", dc.command_id);
        if (strcmp(dc.type, "config-changed") == 0) kind = STAGE_CONFIG;
        else if (strcmp(dc.type, "firmware-update") == 0) kind = STAGE_OTA;
    }
    cJSON_Delete(cmd);
    return kind;
}

static void on_data(esp_mqtt_event_handle_t e)
{
    if (!stage_ensure()) { ESP_LOGE(TAG, "stage alloc failed; dropping %d B", e->total_data_len); return; }
    if (e->total_data_len >= STAGE_CAP) {
        // Every fragment of an oversized message re-enters this branch and bails
        // before touching the slot; log only once, on the message's first fragment.
        if (e->current_data_offset == 0) ESP_LOGE(TAG, "payload %d B exceeds stage cap %d", e->total_data_len, STAGE_CAP);
        return;
    }

    // Always assemble through the staging slot, even a message that arrives in
    // a single fragment — a sparse config with no uploaded images/presigned
    // URLs can plausibly land under the esp-mqtt buffer size, and a separate
    // fast path would silently never stage (and thus never flag) that config.
    xSemaphoreTake(s_stage_mux, portMAX_DELAY);
    if (e->current_data_offset == 0) {
        // A new message starting invalidates any unconsumed prior one, for
        // BOTH staged kinds: without this, a reader could later splice this
        // message's leading bytes onto a previous message's stale tail
        // (checked s_stage_ready/s_ota_ready before this lock, then read a
        // buffer this write is now overwriting).
        s_stage_ready = false;
        s_ota_ready = false;
    }
    memcpy(s_stage + e->current_data_offset, e->data, e->data_len);
    bool complete = (e->current_data_offset + e->data_len >= e->total_data_len);
    if (complete) {
        s_stage_len = e->total_data_len;
        s_stage[s_stage_len] = '\0';
    }
    xSemaphoreGive(s_stage_mux);
    if (!complete) return;

    stage_kind_t kind = on_cmd_message(s_stage, s_stage_len);
    if (kind != STAGE_NONE) {
        // Flag under the mutex too: s_stage_ready/s_ota_ready are only ever
        // touched while holding s_stage_mux, so a reader's authoritative
        // (post-lock) check of either is never stale relative to the buffer
        // it guards.
        xSemaphoreTake(s_stage_mux, portMAX_DELAY);
        if (kind == STAGE_CONFIG) s_stage_ready = true;
        else if (kind == STAGE_OTA) s_ota_ready = true;
        xSemaphoreGive(s_stage_mux);
    }
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
        on_data(e);
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

bool mqtt_config_pending(void) { return s_stage_ready; }

bool mqtt_parse_pending_config(device_config_t *out)
{
    // Cheap early-out; not authoritative. A new message can flip this false
    // (or start overwriting the buffer) at any time from the esp-mqtt task, so
    // it proves nothing about the state of s_stage once the lock is held.
    if (!s_stage_ready || !s_stage || !s_stage_mux) return false;
    xSemaphoreTake(s_stage_mux, portMAX_DELAY);
    bool ok = false;
    // Authoritative recheck under the lock: a message that began arriving
    // between the early-out above and this xSemaphoreTake would have already
    // cleared s_stage_ready (see on_data) and possibly be mid-overwrite of
    // s_stage. Bail rather than parse a buffer spliced from two messages.
    if (s_stage_ready) {
        // The staged bytes are the command envelope; Task B1's extractor pulls the
        // config out of it and cfg_parse_json (unchanged) does the real parse. A
        // false return here includes the legacy `payload: null` nudge, which the
        // caller answers by re-requesting over cfg/get.
        char *payload = NULL;
        if (cmd_payload_json(s_stage, &payload)) {
            ok = cfg_parse_json(payload, out);
            if (ok) cloud_config_cache_write(payload);   // cache the payload, not the envelope
            cJSON_free(payload);
        }
        s_stage_ready = false;
    }
    xSemaphoreGive(s_stage_mux);
    return ok;
}

bool mqtt_parse_pending_ota(fw_manifest_t *out)
{
    // Cheap early-out; not authoritative — see mqtt_parse_pending_config.
    if (!s_ota_ready || !s_stage || !s_stage_mux) return false;
    xSemaphoreTake(s_stage_mux, portMAX_DELAY);
    bool ok = false;
    // Authoritative recheck under the lock, same reasoning as
    // mqtt_parse_pending_config: a message that began arriving between the
    // early-out above and this xSemaphoreTake would have already cleared
    // s_ota_ready (see on_data) and possibly be mid-overwrite of s_stage.
    if (s_ota_ready) {
        char *payload = NULL;
        if (cmd_payload_json(s_stage, &payload)) {
            ok = ota_parse_manifest(payload, out);
            cJSON_free(payload);
        }
        s_ota_ready = false;
    }
    xSemaphoreGive(s_stage_mux);
    return ok;
}

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

void mqtt_publish_cfg_get(void)
{
    if (!s_client || !s_connected) return;
    char topic[80];
    mqtt_topic_cfg_get(topic, sizeof(topic), s_device_id);
    esp_mqtt_client_publish(s_client, topic, "{}", 2, 1, 0);
    ESP_LOGI(TAG, "cfg/get published");
}

void mqtt_publish_hb(const char *version, int fonts, int afetch, int aimg,
                     int cfgimg, int cfgstat, int cfgparse)
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
    if (afetch   > -999) cJSON_AddNumberToObject(j, "afetch",   (double)afetch);
    if (aimg     > -999) cJSON_AddNumberToObject(j, "aimg",     (double)aimg);
    if (cfgimg   > -999) cJSON_AddNumberToObject(j, "cfgimg",   (double)cfgimg);
    if (cfgstat  > -999) cJSON_AddNumberToObject(j, "cfgstat",  (double)cfgstat);
    if (cfgparse > -999) cJSON_AddNumberToObject(j, "cfgparse", (double)cfgparse);
    char *body = cJSON_PrintUnformatted(j);
    if (!body) { cJSON_Delete(j); return; }
    esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);
    cJSON_free(body); cJSON_Delete(j);
}
