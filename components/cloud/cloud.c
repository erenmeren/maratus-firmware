#include "cloud.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "nvs.h"
#include "appcfg.h"
#include "device_config.h"

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

int cloud_post_receipt(const uint8_t *png, size_t png_len, char *url_out, int url_cap)
{
    if (url_out && url_cap > 0) url_out[0] = '\0';
    if (!png || png_len == 0) return -1;

    const char *boundary = "----dittoBoundaryM3xKt7Qw";

    char meta[96];
    snprintf(meta, sizeof(meta), "{\"firmwareVersion\":\"%s\"}", appcfg_fw_version());

    char head[256];
    int head_len = snprintf(head, sizeof(head),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"receipt.png\"\r\n"
        "Content-Type: image/png\r\n\r\n", boundary);

    char tail[256];
    int tail_len = snprintf(tail, sizeof(tail),
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"metadata\"\r\n\r\n"
        "%s"
        "\r\n--%s--\r\n", boundary, meta, boundary);

    size_t total = (size_t)head_len + png_len + (size_t)tail_len;
    char *body = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!body) body = malloc(total);
    if (!body) { ESP_LOGE(TAG, "ingest body alloc failed (%u bytes)", (unsigned)total); return -1; }
    memcpy(body, head, head_len);
    memcpy(body + head_len, png, png_len);
    memcpy(body + head_len + png_len, tail, tail_len);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/ingest", appcfg_base_url());
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", appcfg_device_key());
    char ctype[96];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);

    char respbuf[512];
    respbuf[0] = '\0';
    resp_t resp = { .buf = respbuf, .cap = sizeof(respbuf), .len = 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_evt,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", ctype);
    esp_http_client_set_post_field(c, body, total);

    int status = -1;
    if (esp_http_client_perform(c) == ESP_OK) {
        status = esp_http_client_get_status_code(c);
        respbuf[resp.len] = '\0';
        ESP_LOGI(TAG, "POST /ingest -> %d, body: %s", status, respbuf);
        if (status == 201 && url_out) {
            cJSON *root = cJSON_Parse(respbuf);
            if (root) {
                const char *u = cJSON_GetStringValue(cJSON_GetObjectItem(root, "url"));
                if (u) { strncpy(url_out, u, url_cap - 1); url_out[url_cap - 1] = '\0'; }
                cJSON_Delete(root);
            }
        }
    } else {
        ESP_LOGE(TAG, "POST /ingest failed");
    }
    esp_http_client_cleanup(c);
    free(body);
    return status;
}

// ---- device config (GET /api/device/config) ----

static uint32_t parse_hex_color(const char *s, uint32_t dflt)
{
    if (!s) return dflt;
    if (*s == '#') s++;
    uint32_t v = 0; int n = 0;
    for (; *s && n < 6; s++, n++) {
        char ch = *s; int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return dflt;
        v = (v << 4) | d;
    }
    return n == 6 ? v : dflt;
}

static int align_of(const char *s)
{
    if (s && !strcmp(s, "center")) return 1;
    if (s && !strcmp(s, "right")) return 2;
    return 0;
}

static bool cfg_parse_json(const char *json, device_config_t *cfg)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    const char *ver = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    cfg->etag[0] = '\0';
    if (ver) { strncpy(cfg->etag, ver, sizeof(cfg->etag) - 1); cfg->etag[sizeof(cfg->etag) - 1] = '\0'; }
    cfg->brand_bg = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandBg")), 0x10A765);
    cfg->brand_fg = parse_hex_color(cJSON_GetStringValue(cJSON_GetObjectItem(root, "brandFg")), 0xFFFFFF);
    cfg->n_texts = 0;

    cJSON *config = cJSON_GetObjectItem(root, "config");
    cJSON *screens = config ? cJSON_GetObjectItem(config, "screens") : NULL;
    cJSON *idle = screens ? cJSON_GetObjectItem(screens, "idle") : NULL;
    cJSON *objs = idle ? cJSON_GetObjectItem(idle, "objects") : NULL;
    int n = cJSON_IsArray(objs) ? cJSON_GetArraySize(objs) : 0;
    for (int i = 0; i < n && cfg->n_texts < CFG_MAX_TEXTS; i++) {
        cJSON *o = cJSON_GetArrayItem(objs, i);
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(o, "type"));
        cJSON *vis = cJSON_GetObjectItem(o, "visible");
        if (!type || strcmp(type, "text") != 0) continue;       // M5: text objects only
        if (cJSON_IsBool(vis) && !cJSON_IsTrue(vis)) continue;  // skip hidden
        const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(o, "text"));
        if (!txt) continue;
        cfg_text_t *t = &cfg->texts[cfg->n_texts++];
        t->visible = true;
        t->x = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "x"));
        t->y = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "y"));
        strncpy(t->text, txt, CFG_TEXT_LEN - 1); t->text[CFG_TEXT_LEN - 1] = '\0';
        cJSON *fs = cJSON_GetObjectItem(o, "fontSize");
        t->font_size = cJSON_IsNumber(fs) ? (int)cJSON_GetNumberValue(fs) : 28;
        t->align = align_of(cJSON_GetStringValue(cJSON_GetObjectItem(o, "align")));
    }

    cfg->valid = true;
    cJSON_Delete(root);
    return true;
}

#define CFG_NVS_NS "ditto"

static void cfg_save_nvs(const char *json)
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

int cloud_get_config(device_config_t *cfg)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/config", appcfg_base_url());
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", appcfg_device_key());
    char inm[64] = {0};
    if (cfg->etag[0]) snprintf(inm, sizeof(inm), "\"%s\"", cfg->etag);

    static char body[16384];
    body[0] = '\0';
    resp_t resp = { .buf = body, .cap = sizeof(body), .len = 0 };

    esp_http_client_config_t hc = {
        .url = url, .method = HTTP_METHOD_GET,
        .event_handler = on_evt, .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    esp_http_client_set_header(c, "Authorization", auth);
    if (inm[0]) esp_http_client_set_header(c, "If-None-Match", inm);

    int status = -1;
    if (esp_http_client_perform(c) == ESP_OK) {
        status = esp_http_client_get_status_code(c);
        body[resp.len] = '\0';
        if (status == 200) {
            if (cfg_parse_json(body, cfg)) { cfg_save_nvs(body); ESP_LOGI(TAG, "config updated (%d texts)", cfg->n_texts); }
            else ESP_LOGW(TAG, "config parse failed");
        } else if (status == 304) {
            ESP_LOGI(TAG, "config not modified");
        }
    } else {
        ESP_LOGE(TAG, "GET /config failed");
    }
    esp_http_client_cleanup(c);
    return status;
}
