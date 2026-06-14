#include "barcode.h"

// Canonical Code128 symbol widths (values 0..106). Each string lists the widths
// of alternating bar,space,bar,space,bar,space (and a trailing bar for the stop
// symbol 106). Widths sum to 11 (13 for stop). This is the standard Code128 table.
static const char *CODE128[107] = {
 "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
 "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
 "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
 "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
 "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
 "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
 "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
 "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
 "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
 "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
 "114131","311141","411131","211412","211214","211232","2331112"
};

#define START_B 104
#define STOP    106

int code128b_encode(const char *data, int len, unsigned char *out, int cap)
{
    if (len <= 0) return -1;

    // Symbol values: Start-B, data values, checksum, Stop.
    int vals[300];
    int nv = 0;
    vals[nv++] = START_B;
    long sum = START_B;
    for (int k = 0; k < len && nv < 297; k++) {
        unsigned char ch = (unsigned char)data[k];
        if (ch < 32 || ch > 127) ch = 32;
        int v = ch - 32;            // Code128-B value
        vals[nv++] = v;
        sum += (long)v * (k + 1);   // weight = position (1-based)
    }
    vals[nv++] = (int)(sum % 103);  // checksum
    vals[nv++] = STOP;

    // Expand symbols to module bits.
    int m = 0;
    for (int s = 0; s < nv; s++) {
        const char *pat = CODE128[vals[s]];
        int bar = 1; // each symbol starts with a bar
        for (const char *p = pat; *p; p++) {
            int w = *p - '0';
            for (int j = 0; j < w; j++) { if (m >= cap) return -1; out[m++] = bar ? 1 : 0; }
            bar = !bar;
        }
    }
    return m;
}
