#include "render.h"
#include "png_encode.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "qrcodegen.h"

static const char *TAG = "render";

#define MAX_HEIGHT 20000   // bound memory; over-long jobs truncate
#define QR_QUIET   4       // quiet-zone modules each side (QR spec minimum)

static int x_offset(align_t a, int w)
{
    if (w >= RENDER_ROLL_WIDTH) return 0;
    if (a == ALIGN_CENTER) return (RENDER_ROLL_WIDTH - w) / 2;
    if (a == ALIGN_RIGHT)  return RENDER_ROLL_WIDTH - w;
    return 0;
}

typedef struct {
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    int side;        // modules per side (0 = encode failed)
    int module_px;
} qr_enc_t;

static enum qrcodegen_Ecc ecc_of(uint8_t e)
{
    switch (e) {
        case 1: return qrcodegen_Ecc_MEDIUM;
        case 2: return qrcodegen_Ecc_QUARTILE;
        case 3: return qrcodegen_Ecc_HIGH;
        default: return qrcodegen_Ecc_LOW;
    }
}

// Encode one OP_QR into *enc. Returns true on success.
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

static int qr_px(const qr_enc_t *e) { return (e->side + 2 * QR_QUIET) * e->module_px; }

bool render_ops_to_png(const draw_op_t *ops, int n_ops,
                       uint8_t **png_out, size_t *png_len, int *out_w, int *out_h)
{
    const int W = RENDER_ROLL_WIDTH;

    // Pre-encode every QR op once (indexed by op position; NULL entry = not a QR).
    qr_enc_t **qrs = calloc(n_ops, sizeof(qr_enc_t *));
    if (!qrs) return false;
    for (int i = 0; i < n_ops; i++) {
        if (ops[i].kind != OP_QR) continue;
        qr_enc_t *e = malloc(sizeof(qr_enc_t));
        if (e && qr_encode(&ops[i], e)) qrs[i] = e;
        else { free(e); ESP_LOGW(TAG, "QR encode failed"); }
    }

    // Height: rasters + feeds + QR boxes.
    int H = 0;
    for (int i = 0; i < n_ops; i++) {
        if (ops[i].kind == OP_RASTER) H += ops[i].raster.height;
        else if (ops[i].kind == OP_FEED) H += ops[i].feed.dots;
        else if (ops[i].kind == OP_QR && qrs[i]) H += qr_px(qrs[i]);
    }
    if (H < 1) H = 1;
    if (H > MAX_HEIGHT) H = MAX_HEIGHT;

    size_t fb_len = (size_t)W * H;
    uint8_t *fb = heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) fb = malloc(fb_len);
    if (!fb) {
        ESP_LOGE(TAG, "fb alloc failed (%dx%d)", W, H);
        for (int i = 0; i < n_ops; i++) free(qrs[i]);
        free(qrs);
        return false;
    }
    memset(fb, 0xFF, fb_len); // white

    int cur_y = 0;
    align_t align = ALIGN_LEFT;
    for (int i = 0; i < n_ops && cur_y < H; i++) {
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
                    if (row[x >> 3] & (0x80 >> (x & 7))) fb[(size_t)(cur_y + y) * W + px] = 0x00;
                }
            }
            cur_y += op->raster.height;
        } else if (op->kind == OP_QR && qrs[i]) {
            qr_enc_t *e = qrs[i];
            int box = qr_px(e);
            int xo = x_offset(align, box);
            for (int my = 0; my < e->side; my++) {
                for (int mx = 0; mx < e->side; mx++) {
                    if (!qrcodegen_getModule(e->qr, mx, my)) continue;
                    int bx = xo + (QR_QUIET + mx) * e->module_px;
                    int by = cur_y + (QR_QUIET + my) * e->module_px;
                    for (int dy = 0; dy < e->module_px; dy++) {
                        int yy = by + dy; if (yy < 0 || yy >= H) continue;
                        for (int dx = 0; dx < e->module_px; dx++) {
                            int xx = bx + dx; if (xx < 0 || xx >= W) continue;
                            fb[(size_t)yy * W + xx] = 0x00;
                        }
                    }
                }
            }
            cur_y += box;
        }
    }

    bool ok = png_encode_gray8(fb, W, H, png_out, png_len);
    free(fb);
    for (int i = 0; i < n_ops; i++) free(qrs[i]);
    free(qrs);
    if (ok) { if (out_w) *out_w = W; if (out_h) *out_h = H; }
    return ok;
}
