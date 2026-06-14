#include "render_job.h"
#include "esp_log.h"
#include <stdlib.h>
#include "escpos_parser.h"
#include "render.h"
#include "cloud.h"
#include "ui.h"

static const char *TAG = "job";

#define MAX_OPS 512

void render_job_handle(const uint8_t *data, size_t len)
{
    ui_show(UI_SCREEN_PROCESSING);

    static draw_op_t ops[MAX_OPS];
    int n = escpos_parse(data, len, ops, MAX_OPS);
    if (n <= 0) { ESP_LOGW(TAG, "no drawable ops; ignoring job"); ui_show(UI_SCREEN_IDLE); return; }

    uint8_t *png = NULL; size_t png_len = 0; int w = 0, h = 0;
    if (!render_ops_to_png(ops, n, &png, &png_len, &w, &h)) {
        ESP_LOGE(TAG, "render failed"); ui_show(UI_SCREEN_IDLE); return;
    }
    ESP_LOGI(TAG, "rendered %dx%d -> PNG %u bytes", w, h, (unsigned)png_len);

    static char url[256];
    int status = cloud_post_receipt(png, png_len, url, sizeof(url));
    free(png);

    if (status == 201 && url[0]) {
        ESP_LOGI(TAG, "receipt ready: %s", url);
        ui_show_qr(url);
    } else {
        ESP_LOGW(TAG, "upload failed (status=%d)", status);
        ui_show(UI_SCREEN_IDLE);
    }
}
