#include "render.h"
#include "png_encode.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "render";

#define MAX_HEIGHT 20000   // bound memory; over-long jobs truncate

static int ops_height(const draw_op_t *ops, int n)
{
    int h = 0;
    for (int i = 0; i < n; i++) {
        if (ops[i].kind == OP_RASTER) h += ops[i].raster.height;
        else if (ops[i].kind == OP_FEED) h += ops[i].feed.dots;
    }
    if (h < 1) h = 1;
    if (h > MAX_HEIGHT) h = MAX_HEIGHT;
    return h;
}

static int x_offset(align_t a, int w)
{
    if (w >= RENDER_ROLL_WIDTH) return 0;
    if (a == ALIGN_CENTER) return (RENDER_ROLL_WIDTH - w) / 2;
    if (a == ALIGN_RIGHT)  return RENDER_ROLL_WIDTH - w;
    return 0;
}

bool render_ops_to_png(const draw_op_t *ops, int n_ops,
                       uint8_t **png_out, size_t *png_len, int *out_w, int *out_h)
{
    const int W = RENDER_ROLL_WIDTH;
    int H = ops_height(ops, n_ops);

    size_t fb_len = (size_t)W * H;
    uint8_t *fb = heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) fb = malloc(fb_len);
    if (!fb) { ESP_LOGE(TAG, "fb alloc failed (%dx%d)", W, H); return false; }
    memset(fb, 0xFF, fb_len); // white

    int cur_y = 0;
    align_t align = ALIGN_LEFT;
    for (int i = 0; i < n_ops; i++) {
        const draw_op_t *op = &ops[i];
        if (op->kind == OP_ALIGN) {
            align = op->align.align;
        } else if (op->kind == OP_FEED) {
            cur_y += op->feed.dots;
        } else if (op->kind == OP_RASTER) {
            int xo = x_offset(align, op->raster.width_px);
            for (int y = 0; y < op->raster.height && (cur_y + y) < H; y++) {
                const uint8_t *row = op->raster.bits + (size_t)y * op->raster.width_bytes;
                for (int x = 0; x < op->raster.width_px; x++) {
                    int px = xo + x;
                    if (px < 0 || px >= W) continue;
                    int bit = row[x >> 3] & (0x80 >> (x & 7));
                    if (bit) fb[(size_t)(cur_y + y) * W + px] = 0x00; // black
                }
            }
            cur_y += op->raster.height;
        }
        if (cur_y >= H) break;
    }

    bool ok = png_encode_gray8(fb, W, H, png_out, png_len);
    free(fb);
    if (ok) { if (out_w) *out_w = W; if (out_h) *out_h = H; }
    return ok;
}
