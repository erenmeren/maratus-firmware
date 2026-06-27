#include "png_encode.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

static uint32_t s_crc[256];
static bool s_crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc[n] = c;
    }
    s_crc_ready = true;
}

static uint32_t crc32_buf(const uint8_t *d, size_t n)
{
    if (!s_crc_ready) crc_init();
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = s_crc[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static uint32_t adler32_buf(const uint8_t *d, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

static void put_be32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

// Append a PNG chunk (len, type, data, crc) at *pos; advance *pos.
static void put_chunk(uint8_t *out, size_t *pos, const char *type, const uint8_t *data, uint32_t len)
{
    put_be32(out + *pos, len); *pos += 4;
    size_t type_at = *pos;
    memcpy(out + *pos, type, 4); *pos += 4;
    if (len) { memcpy(out + *pos, data, len); *pos += len; }
    uint32_t crc = crc32_buf(out + type_at, 4 + len);
    put_be32(out + *pos, crc); *pos += 4;
}

// Wrap already-filtered raw scanlines (`raw`, `raw_len`) into a PNG of color
// type 0 (grayscale) at `bit_depth`, using a zlib stream of STORED (uncompressed)
// deflate blocks. Allocates *out (PSRAM-preferred); caller frees. Consumes `raw`
// (freed here on both success and failure) so callers don't double-free.
static bool emit_png(uint8_t *raw, size_t raw_len, int w, int h, int bit_depth,
                     uint8_t **out, size_t *out_len)
{
    size_t nblocks = (raw_len + 65534) / 65535;
    if (nblocks == 0) nblocks = 1;
    size_t idat_cap = 2 + raw_len + 5 * nblocks + 4; // CMF/FLG + blocks(5B hdr each) + adler
    uint8_t *idat = heap_caps_malloc(idat_cap, MALLOC_CAP_SPIRAM);
    if (!idat) idat = malloc(idat_cap);
    if (!idat) { free(raw); return false; }

    size_t p = 0;
    idat[p++] = 0x78; idat[p++] = 0x01; // zlib header
    size_t remaining = raw_len; const uint8_t *src = raw;
    if (remaining == 0) { idat[p++] = 0x01; idat[p++]=0; idat[p++]=0; idat[p++]=0xff; idat[p++]=0xff; }
    while (remaining > 0) {
        size_t blk = remaining > 65535 ? 65535 : remaining;
        idat[p++] = (blk == remaining) ? 0x01 : 0x00;       // BFINAL on last, BTYPE=00 (stored)
        idat[p++] = blk & 0xff; idat[p++] = (blk >> 8) & 0xff;        // LEN (LE)
        idat[p++] = (~blk) & 0xff; idat[p++] = ((~blk) >> 8) & 0xff;  // NLEN
        memcpy(idat + p, src, blk); p += blk;
        src += blk; remaining -= blk;
    }
    uint32_t adler = adler32_buf(raw, raw_len);
    put_be32(idat + p, adler); p += 4; // adler32 is big-endian in zlib
    size_t idat_len = p;

    // Assemble the PNG: signature + IHDR + IDAT + IEND.
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w); put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = (uint8_t)bit_depth;  // bit depth
    ihdr[9] = 0;  // color type 0 = grayscale
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; // compression, filter, interlace

    size_t total = 8 + (12 + 13) + (12 + idat_len) + (12 + 0);
    uint8_t *png = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!png) png = malloc(total);
    if (!png) { free(raw); free(idat); return false; }

    static const uint8_t sig[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    size_t pos = 0;
    memcpy(png + pos, sig, 8); pos += 8;
    put_chunk(png, &pos, "IHDR", ihdr, 13);
    put_chunk(png, &pos, "IDAT", idat, (uint32_t)idat_len);
    put_chunk(png, &pos, "IEND", NULL, 0);

    free(raw); free(idat);
    *out = png; *out_len = pos;
    return true;
}

bool png_encode_gray8(const uint8_t *gray, int w, int h, uint8_t **out, size_t *out_len)
{
    if (!gray || w <= 0 || h <= 0) return false;

    // One filter byte (0 = none) per row, then the row's 8-bit pixels.
    size_t raw_len = (size_t)h * (1 + (size_t)w);
    uint8_t *raw = heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM);
    if (!raw) raw = malloc(raw_len);
    if (!raw) return false;
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (1 + w)] = 0x00;
        memcpy(raw + (size_t)y * (1 + w) + 1, gray + (size_t)y * w, w);
    }
    return emit_png(raw, raw_len, w, h, 8, out, out_len);
}

bool png_encode_mono1(const uint8_t *gray, int w, int h, uint8_t **out, size_t *out_len)
{
    if (!gray || w <= 0 || h <= 0) return false;

    // 1-bit grayscale: PNG packs 8 pixels per byte, MSB-first, sample 1 = white,
    // 0 = black. Document content is strictly bilevel (white bg + black ink) so
    // thresholding at mid-gray is lossless. Each row = 1 filter byte (0 = none)
    // + ceil(w/8) packed bytes; padding bits in the last byte are ignored by the
    // width and left 0.
    size_t stride = ((size_t)w + 7) / 8;
    size_t raw_len = (size_t)h * (1 + stride);
    uint8_t *raw = heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM);
    if (!raw) raw = malloc(raw_len);
    if (!raw) return false;
    memset(raw, 0, raw_len); // filter bytes + black bits default to 0
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + stride) + 1; // skip filter byte
        const uint8_t *gp = gray + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (gp[x] >= 128) row[x >> 3] |= (uint8_t)(0x80u >> (x & 7)); // white bit
        }
    }
    return emit_png(raw, raw_len, w, h, 1, out, out_len);
}
