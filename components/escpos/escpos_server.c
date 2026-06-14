#include "escpos_server.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "escpos-srv";

#define PRINT_PORT      9100
#define JOB_MAX_BYTES   (2 * 1024 * 1024)  // cap a single job at 2 MB
#define IDLE_MS         1500               // gap that marks end-of-job
#define CHUNK           2048

static escpos_job_cb s_cb;

static void handle_conn(int sock)
{
    size_t cap = 64 * 1024, len = 0;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) buf = malloc(cap);
    if (!buf) { ESP_LOGE(TAG, "job buf alloc failed"); return; }

    struct timeval tv = { .tv_sec = IDLE_MS / 1000, .tv_usec = (IDLE_MS % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (;;) {
        if (len + CHUNK > cap) {
            size_t ncap = cap * 2;
            if (ncap > JOB_MAX_BYTES) ncap = JOB_MAX_BYTES;
            if (len + CHUNK > ncap) { ESP_LOGW(TAG, "job too large, truncating"); break; }
            uint8_t *nb = heap_caps_malloc(ncap, MALLOC_CAP_SPIRAM);
            if (!nb) nb = malloc(ncap);
            if (!nb) { ESP_LOGE(TAG, "job grow failed"); break; }
            memcpy(nb, buf, len); free(buf); buf = nb; cap = ncap;
        }
        int r = recv(sock, buf + len, CHUNK, 0);
        if (r > 0) { len += r; }
        else if (r == 0) { break; }                 // peer closed
        else { break; }                             // timeout (idle) or error -> end of job
    }

    ESP_LOGI(TAG, "job received: %u bytes", (unsigned)len);
    if (len > 0 && s_cb) s_cb(buf, len);
    free(buf);
}

static void server_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int yes = 1; setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY),
                                .sin_port = htons(PRINT_PORT) };
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind :%d failed", PRINT_PORT); vTaskDelete(NULL); return;
    }
    listen(listen_sock, 1);
    ESP_LOGI(TAG, "listening on :%d", PRINT_PORT);

    for (;;) {
        struct sockaddr_in peer; socklen_t plen = sizeof(peer);
        int sock = accept(listen_sock, (struct sockaddr *)&peer, &plen);
        if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        ESP_LOGI(TAG, "connection accepted");
        handle_conn(sock);
        close(sock);
    }
}

void escpos_server_start(escpos_job_cb cb)
{
    s_cb = cb;
    xTaskCreate(server_task, "escpos_srv", 8192, NULL, 5, NULL);
}
