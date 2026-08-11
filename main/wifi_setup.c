#include "wifi_setup.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "net.h"
#include "ui.h"
#include "appcfg.h"
#include "wifi_util.h"

static const char *TAG = "wifi-setup";
#define MAX_APS 24

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// idle_timeout_ms > 0: give up and return if the network list has sat untouched
// that long. The budget is cancelled by the first tap that means "someone is
// standing here" (rescan, or picking a network) — past that the flow waits
// indefinitely, like the Settings entry point does.
static void wifi_setup_loop(bool allow_exit, int idle_timeout_ms) {
    static net_ap_t aps[MAX_APS];
    wifi_ap_t uaps[MAX_APS];
    int n = 0;
    char ssid[33] = {0}, pass[64] = {0};
    bool open = false;
    enum { SCAN, LIST, PASSWORD, CONNECT } st = SCAN;
    int64_t deadline = idle_timeout_ms > 0 ? now_ms() + idle_timeout_ms : 0;

    ui_wifi_show();
    ui_wifi_set_exit_visible(allow_exit);
    for (;;) {
        if (st == SCAN) {
            ui_wifi_set_status("Scanning...");
            n = net_scan(aps, MAX_APS);
            // net_ap_t.ssid and wifi_ap_t.ssid are both char[33]; memcpy the whole
            // fixed buffer (terminator included) — equal sizes, no truncation possible.
            for (int i = 0; i < n; i++) { memcpy(uaps[i].ssid, aps[i].ssid, sizeof(uaps[i].ssid)); uaps[i].ssid[32]='\0'; uaps[i].rssi = aps[i].rssi; uaps[i].open = aps[i].open; }
            n = wifi_util_dedupe_sort(uaps, n);
            for (int i = 0; i < n; i++) { memcpy(aps[i].ssid, uaps[i].ssid, sizeof(aps[i].ssid)); aps[i].ssid[32]='\0'; aps[i].rssi = uaps[i].rssi; aps[i].open = uaps[i].open; }
            ui_wifi_set_results(aps, n);
            ui_wifi_set_status(n ? "" : "No networks - tap Rescan");
            st = LIST;
        } else if (st == LIST) {
            int idx; bool manual;
            if (allow_exit && ui_wifi_consume_exit()) return;
            if (deadline && now_ms() >= deadline) {
                ESP_LOGI(TAG, "no one picked a network in time; leaving the picker");
                return;
            }
            if (ui_wifi_consume_rescan()) { deadline = 0; st = SCAN; }
            else if (ui_wifi_consume_selection(&idx, &manual)) {
                deadline = 0;
                if (manual) { open = false; ssid[0] = '\0'; ui_wifi_prompt_ssid(); st = PASSWORD; }
                else if (idx >= 0 && idx < n) {
                    strncpy(ssid, aps[idx].ssid, sizeof(ssid)-1); ssid[sizeof(ssid)-1]='\0'; open = aps[idx].open;
                    if (open) { pass[0] = '\0'; st = CONNECT; }
                    else { ui_wifi_prompt_password(ssid); st = PASSWORD; }
                }
            }
        } else if (st == PASSWORD) {
            char es[33], ep[64];
            if (ui_wifi_consume_back()) {
                ssid[0] = '\0'; pass[0] = '\0'; open = false;
                ui_wifi_show_list();
                ui_wifi_set_status(n ? "" : "No networks - tap Rescan");
                st = LIST;
            } else if (ui_wifi_consume_connect(es, sizeof(es), ep, sizeof(ep))) {
                if (ssid[0] == '\0') {                      // manual SSID just entered
                    strncpy(ssid, es, sizeof(ssid)-1); ssid[sizeof(ssid)-1]='\0';
                    if (!wifi_util_valid_ssid(ssid)) { ui_wifi_set_status("Enter a network name"); ssid[0]='\0'; }
                    else { open = false; ui_wifi_prompt_password(ssid); }  // now ask for password
                } else {
                    strncpy(pass, ep, sizeof(pass)-1); pass[sizeof(pass)-1]='\0';
                    if (!wifi_util_valid_password(open, pass)) { ui_wifi_set_status("Password must be 8+ characters"); }
                    else st = CONNECT;
                }
            }
        } else if (st == CONNECT) {
            ui_wifi_show_connecting(ssid);
            if (net_connect(ssid, pass)) {
                appcfg_store_wifi_creds(ssid, pass);
                ESP_LOGI(TAG, "connected + stored creds for '%s'", ssid);
                return;
            }
            ui_wifi_prompt_password(ssid);
            ui_wifi_set_status("Couldn't connect - check the password");
            st = PASSWORD;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void wifi_setup_guarded(bool allow_exit, int idle_timeout_ms) {
    // An absent AP (stored creds for a network that isn't here) leaves net.c
    // re-issuing esp_wifi_connect() after every disconnect; those attempts
    // collide with the scans this screen lives on, so the list can come up
    // empty exactly when the operator needs it. Park the retry loop for the
    // duration — but only when there is nothing to lose. Entering from
    // Settings on a working device keeps its association untouched.
    bool parked = !net_is_connected();
    if (parked) net_stop();

    wifi_setup_loop(allow_exit, idle_timeout_ms);

    // Left without picking a network (exit chip, or the idle timeout): re-arm
    // the STORED creds so a network that comes back on its own still recovers
    // the device unattended. Re-applying them matters — a failed attempt in
    // the picker left the radio configured for whatever was typed, and the
    // retry loop would otherwise chase that instead.
    if (parked && !net_is_connected())
        net_connect_async(appcfg_wifi_ssid(), appcfg_wifi_password());
}

void wifi_setup_run(bool allow_exit) { wifi_setup_guarded(allow_exit, 0); }

void wifi_setup_run_recovery(int idle_timeout_ms) { wifi_setup_guarded(true, idle_timeout_ms); }
