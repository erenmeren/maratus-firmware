#pragma once
#include <stdint.h>

#define RENDER_ROLL_WIDTH 576   // 80mm @ 203dpi printable dots (M4a fixed)

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } align_t;

typedef enum {
    OP_RASTER,   // a 1bpp bit-image (GS v 0)
    OP_FEED,     // vertical whitespace, in dots
    OP_ALIGN,    // set alignment for subsequent rasters
    OP_QR,       // a QR code (GS ( k)
    OP_TEXT,     // a styled run of printable characters
    OP_LINE_BREAK, // end the current text line (LF)
    OP_BARCODE,  // a Code128 barcode (GS k)
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
        struct {
            const char *text;  // points INTO the job buffer
            int len;
            uint8_t sx, sy;    // size multipliers (1..4)
            uint8_t bold;
        } text;
        struct {
            const char *data;  // points INTO the job buffer
            int len;
            int height;        // bar height in dots
            int module_w;      // narrowest module width in dots (2..6)
            uint8_t hri;       // 0 = none, else print human-readable text below
        } barcode;
    };
} draw_op_t;
