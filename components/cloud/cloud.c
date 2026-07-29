#include "cloud.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "appcfg.h"
#include "device_config.h"
#include "cfg_parse.h"
#include "provisioning.h"

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

bool cloud_claim_poll(const char *code, char *key_out, size_t key_len)
{
    if (key_out && key_len) key_out[0] = '\0';
    char body[256];
    resp_t resp = { .buf = body, .cap = sizeof(body), .len = 0 };
    char serial[13];
    provisioning_get_serial(serial);
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/claim?code=%s&serial=%s",
             appcfg_base_url(), code, serial);
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_GET, .event_handler = on_evt,
        .user_data = &resp, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    bool got = false;
    if (esp_http_client_perform(c) == ESP_OK) {
        int status = esp_http_client_get_status_code(c);
        body[resp.len] = '\0';
        ESP_LOGI(TAG, "GET /claim -> %d", status);
        if (status == 200) got = provisioning_parse_claim(body, key_out, key_len);
    } else {
        ESP_LOGW(TAG, "GET /claim failed");
    }
    esp_http_client_cleanup(c);
    return got;
}

// ---- raw asset fetch (presigned absolute URL → PSRAM buffer) ----

#define ASSET_MAX_BYTES (256 * 1024)

// Accumulates the response body into a growable PSRAM buffer, capped at
// ASSET_MAX_BYTES. On overflow it sets cap=-1 (sticky) so the perform aborts logic.
typedef struct { uint8_t *buf; size_t len; size_t cap; bool overflow; } asset_resp_t;

static esp_err_t on_asset_evt(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    asset_resp_t *r = (asset_resp_t *)e->user_data;
    if (!r || r->overflow) return ESP_OK;
    if (r->len + (size_t)e->data_len > ASSET_MAX_BYTES) { r->overflow = true; return ESP_OK; }
    // Grow geometrically (start 16 KB) to avoid per-chunk reallocs.
    if (r->len + (size_t)e->data_len > r->cap) {
        size_t ncap = r->cap ? r->cap : 16 * 1024;
        while (ncap < r->len + (size_t)e->data_len) ncap *= 2;
        if (ncap > ASSET_MAX_BYTES) ncap = ASSET_MAX_BYTES;
        uint8_t *nb = heap_caps_realloc(r->buf, ncap, MALLOC_CAP_SPIRAM);
        if (!nb) { r->overflow = true; return ESP_OK; }
        r->buf = nb; r->cap = ncap;
    }
    memcpy(r->buf + r->len, e->data, e->data_len);
    r->len += e->data_len;
    return ESP_OK;
}

// Diagnostic: outcome of the most recent asset GET, reported on the heartbeat.
// -1000 = never attempted; -1 = transport/TLS perform error; -2 = too large;
// else the HTTP status (200 ok, 403 expired-url, …).
static volatile int s_last_asset_status = -1000;
int cloud_last_asset_status(void) { return s_last_asset_status; }

esp_err_t cloud_fetch_asset(const char *url, uint8_t **out, size_t *out_len)
{
    if (!url || !out || !out_len) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    asset_resp_t resp = { 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_asset_evt,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;

    esp_err_t ret = ESP_FAIL;
    esp_err_t err = esp_http_client_perform(c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "asset GET failed: %s", esp_err_to_name(err));
        s_last_asset_status = -1;
    } else if (resp.overflow) {
        ESP_LOGE(TAG, "asset too large (> %d bytes)", ASSET_MAX_BYTES);
        s_last_asset_status = -2;
    } else {
        int status = esp_http_client_get_status_code(c);
        s_last_asset_status = status;
        if (status == 200 && resp.buf && resp.len > 0) {
            ESP_LOGI(TAG, "asset GET -> 200, %u bytes", (unsigned)resp.len);
            *out = resp.buf;
            *out_len = resp.len;
            resp.buf = NULL;   // ownership transferred to caller
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "asset GET -> %d (len %u)", status, (unsigned)resp.len);
        }
    }

    esp_http_client_cleanup(c);
    if (resp.buf) heap_caps_free(resp.buf);
    return ret;
}

// ---- device config cache (NVS) ----
// The config itself now arrives over MQTT (reassembled in PSRAM, parsed by
// cfg_parse_json in app_state.c); this is just the on-disk cache the MQTT
// path writes through and boot seeds from.

#define CFG_NVS_NS "ditto"

void cloud_config_cache_write(const char *json)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "cfg_json", json);
    nvs_commit(h);
    nvs_close(h);
}

void cloud_config_load_cached(device_config_t *cfg)
{
    cfg->valid = false;
    cfg->etag[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = 0;
    if (nvs_get_str(h, "cfg_json", NULL, &len) == ESP_OK && len > 0 && len < 16384) {
        char *buf = malloc(len);
        if (buf && nvs_get_str(h, "cfg_json", buf, &len) == ESP_OK) cfg_parse_json(buf, cfg);
        free(buf);
    }
    nvs_close(h);
}

// Diagnostics for the most recent HTTP config fetch, reported on the heartbeat.
// Config now arrives via MQTT push (parsed directly in app_state.c) so nothing
// writes these anymore; they stay pinned at -1000 ("none yet"). Kept as a
// stable heartbeat wire field rather than reshaping the payload.
// -1000 = none yet. status: HTTP code (200/304) or -1 transport error.
// parse_ok: 1 last 200 parsed valid, 0 last 200 failed to parse, -1000 no 200 yet.
static volatile int s_last_config_status = -1000;
static volatile int s_last_parse_ok = -1000;
int cloud_last_config_status(void) { return s_last_config_status; }
int cloud_last_parse_ok(void) { return s_last_parse_ok; }

static void (*s_cfg_changed_cb)(void);
void cloud_set_config_changed_cb(void (*cb)(void)) { s_cfg_changed_cb = cb; }
void (*cloud_config_changed_handler(void))(void) { return s_cfg_changed_cb; }

static void (*s_fw_update_cb)(void);
void cloud_set_firmware_update_cb(void (*cb)(void)) { s_fw_update_cb = cb; }
void (*cloud_firmware_update_handler(void))(void) { return s_fw_update_cb; }

static void (*s_trigger_cb)(const char *url) = NULL;
void cloud_set_trigger_cb(void (*cb)(const char *url)) { s_trigger_cb = cb; }
void (*cloud_trigger_handler(void))(const char *url) { return s_trigger_cb; }
