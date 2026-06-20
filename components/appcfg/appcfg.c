#include "appcfg.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdbool.h>
#include "nvs.h"
#include "esp_random.h"
#include "provisioning.h"

#define DITTO_NS "ditto"
static char s_dev_key[80];
static char s_pair[16];
static char s_wifi_ssid[33];
static char s_wifi_pass[64];

static bool nvs_read(const char *key, char *out, size_t cap) {
    nvs_handle_t h; out[0] = '\0';
    if (nvs_open(DITTO_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t e = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return e == ESP_OK && out[0];
}
static void nvs_write(const char *key, const char *val) {
    nvs_handle_t h;
    if (nvs_open(DITTO_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val); nvs_commit(h); nvs_close(h);
}
static uint8_t rand_byte(void) { return (uint8_t)esp_random(); }

const char *appcfg_base_url(void)   { return CONFIG_DITTO_API_BASE_URL; }
const char *appcfg_fw_version(void) { return CONFIG_DITTO_FW_VERSION; }
int appcfg_roll_width(void)         { return CONFIG_DITTO_ROLL_WIDTH; }

bool appcfg_has_device_key(void) { char tmp[80]; return nvs_read("device_key", tmp, sizeof(tmp)); }
void appcfg_store_device_key(const char *key) { if (key && key[0]) nvs_write("device_key", key); }
const char *appcfg_device_key(void) {
    if (nvs_read("device_key", s_dev_key, sizeof(s_dev_key))) return s_dev_key;
    return CONFIG_DITTO_DEVICE_KEY;
}
const char *appcfg_pairing_code(void) {
    if (s_pair[0]) return s_pair;
    if (nvs_read("pairing_code", s_pair, sizeof(s_pair))) return s_pair;
    provisioning_make_code(s_pair, sizeof(s_pair), rand_byte);
    nvs_write("pairing_code", s_pair);
    return s_pair;
}

bool appcfg_has_wifi_creds(void) {
    char tmp[33];
    return nvs_read("wifi_ssid", tmp, sizeof(tmp));
}
void appcfg_store_wifi_creds(const char *ssid, const char *pass) {
    if (ssid && ssid[0]) nvs_write("wifi_ssid", ssid);
    nvs_write("wifi_pass", pass ? pass : "");
}
const char *appcfg_wifi_ssid(void) {
    if (nvs_read("wifi_ssid", s_wifi_ssid, sizeof(s_wifi_ssid))) return s_wifi_ssid;
    return CONFIG_DITTO_WIFI_SSID;
}
const char *appcfg_wifi_password(void) {
    if (nvs_read("wifi_pass", s_wifi_pass, sizeof(s_wifi_pass))) return s_wifi_pass;
    return CONFIG_DITTO_WIFI_PASSWORD;
}
