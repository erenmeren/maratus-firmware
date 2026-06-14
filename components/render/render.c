#include "render.h"
#include "png_encode.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "qrcodegen.h"

static const char *TAG = "render";

#define MAX_HEIGHT 20000
#define QR_QUIET   4

static int s_roll_width = RENDER_ROLL_WIDTH;
void render_set_roll_width(int dots) { if (dots >= 128 && dots <= 2048) s_roll_width = dots; }

// ---- growable grayscale canvas (white background) ----
typedef struct { uint8_t *px; int W; int cur_y; int cap; bool oom; } canvas_t;

static bool canvas_ensure(canvas_t *c, int rows_needed)
{
    int want = c->cur_y + rows_needed;
    if (want <= c->cap) return true;
    if (want > MAX_HEIGHT) want = MAX_HEIGHT;
    int ncap = c->cap ? c->cap : 256;
    while (ncap < want) ncap *= 2;
    if (ncap > MAX_HEIGHT) ncap = MAX_HEIGHT;
    uint8_t *np = heap_caps_malloc((size_t)c->W * ncap, MALLOC_CAP_SPIRAM);
    if (!np) np = malloc((size_t)c->W * ncap);
    if (!np) { c->oom = true; return false; }
    memset(np, 0xFF, (size_t)c->W * ncap);
    if (c->px) { memcpy(np, c->px, (size_t)c->W * c->cur_y); free(c->px); }
    c->px = np; c->cap = ncap;
    return true;
}

static void canvas_set(canvas_t *c, int x, int y, uint8_t v)
{
    if (x < 0 || x >= c->W || y < 0 || y >= c->cap) return;
    c->px[(size_t)y * c->W + x] = v;
}

static int x_offset(align_t a, int w, int W)
{
    if (w >= W) return 0;
    if (a == ALIGN_CENTER) return (W - w) / 2;
    if (a == ALIGN_RIGHT)  return W - w;
    return 0;
}

// ---- QR ----
typedef struct {
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    int side; int module_px;
} qr_enc_t;

static enum qrcodegen_Ecc ecc_of(uint8_t e)
{
    switch (e) { case 1: return qrcodegen_Ecc_MEDIUM; case 2: return qrcodegen_Ecc_QUARTILE;
                 case 3: return qrcodegen_Ecc_HIGH; default: return qrcodegen_Ecc_LOW; }
}

static bool qr_encode(const draw_op_t *op, qr_enc_t *enc)
{
    char text[512];
    int n = op->qr.len; if (n > (int)sizeof(text) - 1) n = sizeof(text) - 1;
    memcpy(text, op->qr.data, n); text[n] = '\0';
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    bool ok = qrcodegen_encodeText(text, tmp, enc->qr, ecc_of(op->qr.ecc),
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);
    enc->side = ok ? qrcodegen_getSize(enc->qr) : 0;
    enc->module_px = op->qr.module_px < 1 ? 4 : (op->qr.module_px > 16 ? 16 : op->qr.module_px);
    return ok;
}

bool render_ops_to_png(const draw_op_t *ops, int n_ops,
                       uint8_t **png_out, size_t *png_len, int *out_w, int *out_h)
{
    canvas_t c = { .W = s_roll_width };
    align_t align = ALIGN_LEFT;

    for (int i = 0; i < n_ops && !c.oom; i++) {
        const draw_op_t *op = &ops[i];
        if (op->kind == OP_ALIGN) {
            align = op->align.align;
        } else if (op->kind == OP_FEED) {
            if (canvas_ensure(&c, op->feed.dots)) c.cur_y += op->feed.dots;
        } else if (op->kind == OP_RASTER) {
            int h = op->raster.height;
            if (!canvas_ensure(&c, h)) break;
            int xo = x_offset(align, op->raster.width_px, c.W);
            for (int y = 0; y < h; y++) {
                const uint8_t *row = op->raster.bits + (size_t)y * op->raster.width_bytes;
                for (int x = 0; x < op->raster.width_px; x++)
                    if (row[x >> 3] & (0x80 >> (x & 7))) canvas_set(&c, xo + x, c.cur_y + y, 0x00);
            }
            c.cur_y += h;
        } else if (op->kind == OP_QR) {
            qr_enc_t *e = malloc(sizeof(qr_enc_t));
            if (!e || !qr_encode(op, e)) { free(e); ESP_LOGW(TAG, "QR encode failed"); continue; }
            int box = (e->side + 2 * QR_QUIET) * e->module_px;
            if (!canvas_ensure(&c, box)) { free(e); break; }
            int xo = x_offset(align, box, c.W);
            for (int my = 0; my < e->side; my++)
                for (int mx = 0; mx < e->side; mx++)
                    if (qrcodegen_getModule(e->qr, mx, my))
                        for (int dy = 0; dy < e->module_px; dy++)
                            for (int dx = 0; dx < e->module_px; dx++)
                                canvas_set(&c, xo + (QR_QUIET + mx) * e->module_px + dx,
                                               c.cur_y + (QR_QUIET + my) * e->module_px + dy, 0x00);
            c.cur_y += box;
            free(e);
        }
    }

    if (c.oom || !c.px) { free(c.px); ESP_LOGE(TAG, "canvas alloc failed"); return false; }
    int H = c.cur_y > 0 ? c.cur_y : 1;
    if (H > c.cap) H = c.cap;

    bool ok = png_encode_gray8(c.px, c.W, H, png_out, png_len);
    free(c.px);
    if (ok) { if (out_w) *out_w = c.W; if (out_h) *out_h = H; }
    return ok;
}
