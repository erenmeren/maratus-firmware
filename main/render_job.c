#include "render_job.h"
#include "esp_log.h"
#include <stdlib.h>
#include "escpos_parser.h"
#include "render.h"
#include "cloud.h"
#include "ui.h"
#include "state.h"

static const char *TAG = "job";

#define MAX_OPS 512

// Drive both the UI and app_state's logical state together, so a config-changed
// re-render (poll_task paints ui_render_state(s_state)) preserves whatever this
// receipt job is showing instead of clobbering it back to idle.
static void go(dev_state_t st)
{
    app_state_set_state(st);
    ui_render_state(st);
}

void render_job_handle(const uint8_t *data, size_t len)
{
    go(DEV_PROCESSING);

    static draw_op_t ops[MAX_OPS];
    int n = escpos_parse(data, len, ops, MAX_OPS);
    if (n <= 0) { ESP_LOGW(TAG, "no drawable ops; ignoring job"); go(DEV_IDLE); return; }

    uint8_t *png = NULL; size_t png_len = 0; int w = 0, h = 0;
    if (!render_ops_to_png(ops, n, &png, &png_len, &w, &h)) {
        ESP_LOGE(TAG, "render failed"); go(DEV_IDLE); return;
    }
    ESP_LOGI(TAG, "rendered %dx%d -> PNG %u bytes", w, h, (unsigned)png_len);

    static char url[256];
    int status = cloud_post_receipt(png, png_len, url, sizeof(url));
    free(png);

    if (status == 201 && url[0]) {
        ESP_LOGI(TAG, "receipt ready: %s", url);
        // The QR's countdown arms from s_cd_pending (kept current by app_state on
        // each config apply) when the screen builds, and auto-returns to idle via
        // the expiry flag poll_task watches. Publishing s_state=DEV_QR keeps a
        // mid-receipt config refresh on the QR instead of clobbering it to idle.
        ui_set_qr_url(url);
        go(DEV_QR);
    } else {
        ESP_LOGW(TAG, "upload failed (status=%d)", status);
        go(DEV_IDLE);
    }
}
