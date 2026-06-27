#include "render.h"
#include "png_encode.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "qrcodegen.h"
#include "font8x8_basic.h"
#include "barcode.h"

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

// ---- text: glyph blit + monospace line layout ----
#define GLYPH_W 8
#define GLYPH_H 8
#define LINE_PAD 4
#define MAX_LINE_GLYPHS 256

typedef struct { uint8_t ch, sx, sy, bold; } glyph_t;
typedef struct { glyph_t g[MAX_LINE_GLYPHS]; int n; int width; } line_t;

static void blit_glyph(canvas_t *c, int x, int y, uint8_t ch, int sx, int sy, bool bold)
{
    if (ch < 0x20 || ch > 0x7e) ch = 0x20;
    const char *g = font8x8_basic[(int)ch];
    for (int row = 0; row < GLYPH_H; row++) {
        uint8_t bits = (uint8_t)g[row];
        for (int col = 0; col < GLYPH_W; col++) {
            if (!(bits & (1 << col))) continue;   // font8x8: bit0 = leftmost
            for (int dy = 0; dy < sy; dy++)
                for (int dx = 0; dx < sx; dx++) {
                    canvas_set(c, x + col * sx + dx, y + row * sy + dy, 0x00);
                    if (bold) canvas_set(c, x + col * sx + dx + 1, y + row * sy + dy, 0x00);
                }
        }
    }
}

static void flush_line(canvas_t *c, line_t *ln, align_t align)
{
    if (ln->n == 0) {
        int lh = GLYPH_H + LINE_PAD;
        if (canvas_ensure(c, lh)) c->cur_y += lh;
        return;
    }
    int max_sy = 1;
    for (int i = 0; i < ln->n; i++) if (ln->g[i].sy > max_sy) max_sy = ln->g[i].sy;
    int line_h = GLYPH_H * max_sy + LINE_PAD;
    if (!canvas_ensure(c, line_h)) { ln->n = 0; ln->width = 0; return; }

    int x = x_offset(align, ln->width, c->W);
    for (int i = 0; i < ln->n; i++) {
        glyph_t *g = &ln->g[i];
        blit_glyph(c, x, c->cur_y, g->ch, g->sx, g->sy, g->bold);
        x += GLYPH_W * g->sx;
    }
    c->cur_y += line_h;
    ln->n = 0; ln->width = 0;
}

static void line_putc(canvas_t *c, line_t *ln, align_t align, uint8_t ch, int sx, int sy, bool bold)
{
    int gw = GLYPH_W * sx;
    if (ln->n > 0 && (ln->width + gw > c->W || ln->n >= MAX_LINE_GLYPHS)) flush_line(c, ln, align);
    if (ln->n < MAX_LINE_GLYPHS) {
        ln->g[ln->n++] = (glyph_t){ ch, (uint8_t)sx, (uint8_t)sy, (uint8_t)bold };
        ln->width += gw;
    }
}

// ---- Code128 barcode (bars + HRI digits) ----
#define BC_QUIET 10          // quiet-zone modules each side
#define BC_MAX_MODULES 1200

static void render_barcode(canvas_t *c, align_t align, const draw_op_t *op)
{
    static unsigned char bits[BC_MAX_MODULES];
    int m = code128b_encode(op->barcode.data, op->barcode.len, bits, BC_MAX_MODULES);
    if (m <= 0) { ESP_LOGW(TAG, "barcode encode failed"); return; }

    int mw = op->barcode.module_w < 2 ? 2 : (op->barcode.module_w > 6 ? 6 : op->barcode.module_w);
    int bh = op->barcode.height < 16 ? 16 : (op->barcode.height > 400 ? 400 : op->barcode.height);
    int total_w = (m + 2 * BC_QUIET) * mw;
    int hri_h = op->barcode.hri ? (GLYPH_H + 4) : 0;
    if (!canvas_ensure(c, bh + hri_h)) return;

    int x = x_offset(align, total_w, c->W) + BC_QUIET * mw;
    for (int k = 0; k < m; k++) {
        if (bits[k])
            for (int dx = 0; dx < mw; dx++)
                for (int y = 0; y < bh; y++)
                    canvas_set(c, x + dx, c->cur_y + y, 0x00);
        x += mw;
    }

    if (op->barcode.hri) {
        int text_w = op->barcode.len * GLYPH_W;
        int tx = (text_w < total_w)
               ? x_offset(align, total_w, c->W) + (total_w - text_w) / 2
               : x_offset(align, text_w, c->W);
        for (int k = 0; k < op->barcode.len; k++)
            blit_glyph(c, tx + k * GLYPH_W, c->cur_y + bh + 2, (uint8_t)op->barcode.data[k], 1, 1, false);
    }
    c->cur_y += bh + hri_h;
}

bool render_ops_to_png(const draw_op_t *ops, int n_ops,
                       uint8_t **png_out, size_t *png_len, int *out_w, int *out_h)
{
    canvas_t c = { .W = s_roll_width };
    align_t align = ALIGN_LEFT;
    static line_t ln; ln.n = 0; ln.width = 0;

    for (int i = 0; i < n_ops && !c.oom; i++) {
        const draw_op_t *op = &ops[i];
        if (op->kind == OP_ALIGN) {
            align = op->align.align;
        } else if (op->kind == OP_TEXT) {
            int sx = op->text.sx < 1 ? 1 : (op->text.sx > 4 ? 4 : op->text.sx);
            int sy = op->text.sy < 1 ? 1 : (op->text.sy > 4 ? 4 : op->text.sy);
            for (int k = 0; k < op->text.len; k++)
                line_putc(&c, &ln, align, (uint8_t)op->text.text[k], sx, sy, op->text.bold);
        } else if (op->kind == OP_LINE_BREAK) {
            flush_line(&c, &ln, align);
        } else if (op->kind == OP_BARCODE) {
            flush_line(&c, &ln, align);
            render_barcode(&c, align, op);
        } else if (op->kind == OP_FEED) {
            flush_line(&c, &ln, align);
            if (canvas_ensure(&c, op->feed.dots)) c.cur_y += op->feed.dots;
        } else if (op->kind == OP_RASTER) {
            flush_line(&c, &ln, align);
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
            flush_line(&c, &ln, align);
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

    flush_line(&c, &ln, align);

    if (c.oom || !c.px) { free(c.px); ESP_LOGE(TAG, "canvas alloc failed"); return false; }
    int H = c.cur_y > 0 ? c.cur_y : 1;
    if (H > c.cap) H = c.cap;

    // 1-bit PNG: document content is bilevel (white bg + black ink), so this is
    // lossless and ~8x smaller than gray8 (uncompressed payload over the wire).
    bool ok = png_encode_mono1(c.px, c.W, H, png_out, png_len);
    free(c.px);
    if (ok) { if (out_w) *out_w = c.W; if (out_h) *out_h = H; }
    return ok;
}
