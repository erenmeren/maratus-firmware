// Host harness for png_encode: build a known bilevel pattern, encode it as both
// gray8 and mono1, dump the source pattern + both PNGs for validate.py to check.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png_encode.h"

#define W 100
#define H 60

static void dump(const char *path, const uint8_t *d, size_t n) {
    FILE *f = fopen(path, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

int main(void) {
    uint8_t *gray = malloc(W * H);
    // White (0xFF) background with black (0x00) ink: border, diagonal, and a
    // filled block — representative bilevel document content.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int black = (x == 0 || y == 0 || x == W - 1 || y == H - 1) // border
                     || (x == y)                                       // diagonal
                     || (x >= 10 && x < 30 && y >= 10 && y < 25);      // block
            gray[y * W + x] = black ? 0x00 : 0xFF;
        }
    }
    // source pattern (w,h header as 2 LE uint32, then w*h bytes)
    uint8_t hdr[8];
    uint32_t w = W, h = H;
    memcpy(hdr, &w, 4); memcpy(hdr + 4, &h, 4);
    FILE *sf = fopen("source.bin", "wb");
    fwrite(hdr, 1, 8, sf); fwrite(gray, 1, W * H, sf); fclose(sf);

    uint8_t *p8 = NULL, *p1 = NULL; size_t n8 = 0, n1 = 0;
    if (!png_encode_gray8(gray, W, H, &p8, &n8)) { fprintf(stderr, "gray8 failed\n"); return 1; }
    if (!png_encode_mono1(gray, W, H, &p1, &n1)) { fprintf(stderr, "mono1 failed\n"); return 1; }
    dump("gray8.png", p8, n8);
    dump("mono1.png", p1, n1);
    printf("gray8 %zu bytes, mono1 %zu bytes (%.1fx smaller)\n", n8, n1, (double)n8 / (double)n1);
    free(gray); free(p8); free(p1);
    return 0;
}
