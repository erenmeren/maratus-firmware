#include "time_sync.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_netif_sntp.h"
#include "esp_log.h"

static const char *TAG = "time_sync";
static volatile bool s_synced = false;
static bool s_started = false;

static void on_sync(struct timeval *tv) {
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP time acquired");
}

void time_sync_start(void) {
    if (s_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    esp_err_t rc = esp_netif_sntp_init(&cfg);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(rc));
        return;   // leave s_started false so the next online poll retries
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started");
}

void time_sync_set_tz(const char *posix_tz) {
    const char *tz = (posix_tz && posix_tz[0]) ? posix_tz : "UTC0";
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "TZ set to %s", tz);
}

bool time_is_synced(void) { return s_synced; }
