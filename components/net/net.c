#include "net.h"
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char *TAG = "net";
static EventGroupHandle_t s_events;
#define GOT_IP_BIT BIT0
static volatile bool s_connected = false;
static volatile bool s_want_connect = false;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // intentionally do not auto-connect; net_connect() drives connection
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected (reason %d), reconnecting", d ? d->reason : -1);
        if (s_want_connect) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        ESP_LOGI(TAG, "got IP");
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

void net_init(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

int net_scan(net_ap_t *out, int max)
{
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) { ESP_LOGW(TAG, "scan_start failed"); return 0; }
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;
    static wifi_ap_record_t recs[24];
    uint16_t want = num > 24 ? 24 : num;
    if (esp_wifi_scan_get_ap_records(&want, recs) != ESP_OK) { ESP_LOGW(TAG, "get_ap_records failed"); return 0; }
    int n = 0;
    for (int i = 0; i < want && n < max; i++) {
        strncpy(out[n].ssid, (char *)recs[i].ssid, sizeof(out[n].ssid) - 1);
        out[n].ssid[sizeof(out[n].ssid) - 1] = '\0';
        out[n].rssi = recs[i].rssi;
        out[n].open = (recs[i].authmode == WIFI_AUTH_OPEN);
        n++;
    }
    return n;
}

void net_connect_async(const char *ssid, const char *pass)
{
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid ? ssid : "", sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    s_want_connect = true;
    s_connected = false;
    xEventGroupClearBits(s_events, GOT_IP_BIT);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_LOGI(TAG, "connecting to SSID '%s'", ssid ? ssid : "");
    esp_wifi_connect();
}

bool net_connect(const char *ssid, const char *pass)
{
    net_connect_async(ssid, pass);
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
    return (bits & GOT_IP_BIT) != 0;
}

// The on_event retry loop above re-issues esp_wifi_connect() after every
// disconnect, forever — which is what a store device wants, but it fights
// esp_wifi_scan_start() (a connect attempt in flight makes the scan fail or
// come back empty). The Wi-Fi picker parks it while it is up.
void net_stop(void)
{
    s_want_connect = false;   // set first: no retry follows the disconnect below
    esp_wifi_disconnect();
}

bool net_is_connected(void) { return s_connected; }
