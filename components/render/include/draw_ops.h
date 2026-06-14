#pragma once
#include <stdint.h>

#define RENDER_ROLL_WIDTH 576   // 80mm @ 203dpi printable dots (M4a fixed)

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } align_t;

typedef enum {
    OP_RASTER,   // a 1bpp bit-image (GS v 0)
    OP_FEED,     // vertical whitespace, in dots
    OP_ALIGN,    // set alignment for subsequent rasters
    OP_QR,       // a QR code (GS ( k)
} op_kind_t;

typedef struct {
    op_kind_t kind;
    union {
        struct {
            const uint8_t *bits; // packed 1bpp, MSB-first; 1 = black
            int width_bytes;     // bytes per row
            int width_px;        // width_bytes * 8
            int height;          // dot rows
        } raster;
        struct { int dots; } feed;
        struct { align_t align; } align;
        struct {
            const char *data;  // payload bytes (point INTO the job buffer)
            int len;           // payload length
            int module_px;     // dots per QR module (1..16)
            uint8_t ecc;       // 0=L, 1=M, 2=Q, 3=H
        } qr;
    };
} draw_op_t;
