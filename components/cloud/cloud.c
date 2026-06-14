#include "cloud.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "appcfg.h"

static const char *TAG = "cloud";

typedef struct { char *buf; int cap; int len; } resp_t;

static esp_err_t on_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        resp_t *r = (resp_t *)e->user_data;
        if (r && r->buf) {
            int n = e->data_len;
            if (r->len + n > r->cap - 1) n = r->cap - 1 - r->len;
            if (n > 0) { memcpy(r->buf + r->len, e->data, n); r->len += n; }
        }
    }
    return ESP_OK;
}

int cloud_get_commands(char *out, int cap)
{
    if (out && cap > 0) out[0] = '\0';
    resp_t resp = { .buf = out, .cap = cap, .len = 0 };

    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/commands", appcfg_base_url());

    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", appcfg_device_key());

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_evt,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "x-device-version", appcfg_fw_version());

    int status = -1;
    esp_err_t err = esp_http_client_perform(c);
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(c);
        if (out) out[resp.len] = '\0';
        ESP_LOGI(TAG, "GET /commands -> %d, body: %s", status, out ? out : "");
    } else {
        ESP_LOGE(TAG, "GET /commands failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
    return status;
}

int cloud_ack_command(const char *command_id, bool ok)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/commands/ack", appcfg_base_url());
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", appcfg_device_key());
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"commandId\":\"%s\",\"ok\":%s}",
             command_id, ok ? "true" : "false");

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, payload, strlen(payload));

    int status = -1;
    if (esp_http_client_perform(c) == ESP_OK) status = esp_http_client_get_status_code(c);
    else ESP_LOGE(TAG, "ack %s failed", command_id);
    esp_http_client_cleanup(c);
    return status;
}
