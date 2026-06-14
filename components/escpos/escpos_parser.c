#include "escpos_parser.h"
#include "esp_log.h"

static const char *TAG = "escpos";

#define LINE_FEED_DOTS 30   // default whitespace for LF / one "line"

int escpos_parse(const uint8_t *data, size_t len, draw_op_t *ops, int max_ops)
{
    int n = 0;
    size_t i = 0;

    #define EMIT(op) do { if (n < max_ops) ops[n++] = (op); } while (0)

    while (i < len && n < max_ops) {
        uint8_t b = data[i];

        if (b == 0x0A) { // LF
            EMIT(((draw_op_t){ .kind = OP_FEED, .feed = { LINE_FEED_DOTS } }));
            i++;
        } else if (b == 0x1B) { // ESC ...
            if (i + 1 >= len) break;
            uint8_t c = data[i + 1];
            if (c == 0x40) { // ESC @ reset
                EMIT(((draw_op_t){ .kind = OP_ALIGN, .align = { ALIGN_LEFT } }));
                i += 2;
            } else if (c == 0x61) { // ESC a n  align
                if (i + 2 >= len) break;
                uint8_t nn = data[i + 2];
                align_t a = (nn == 1 || nn == 49) ? ALIGN_CENTER
                          : (nn == 2 || nn == 50) ? ALIGN_RIGHT : ALIGN_LEFT;
                EMIT(((draw_op_t){ .kind = OP_ALIGN, .align = { a } }));
                i += 3;
            } else if (c == 0x4A) { // ESC J n  feed n dots
                if (i + 2 >= len) break;
                EMIT(((draw_op_t){ .kind = OP_FEED, .feed = { data[i + 2] } }));
                i += 3;
            } else if (c == 0x64) { // ESC d n  feed n lines
                if (i + 2 >= len) break;
                EMIT(((draw_op_t){ .kind = OP_FEED, .feed = { data[i + 2] * LINE_FEED_DOTS } }));
                i += 3;
            } else {
                ESP_LOGW(TAG, "skip ESC 0x%02x", c);
                i += 2; // best-effort (M4c hardens unknown-command skipping)
            }
        } else if (b == 0x1D) { // GS ...
            if (i + 1 >= len) break;
            uint8_t c = data[i + 1];
            if (c == 0x76 && i + 2 < len && data[i + 2] == 0x30) { // GS v 0  raster
                if (i + 7 >= len) break;
                // GS v 0 m xL xH yL yH d1..dk
                int width_bytes = data[i + 4] + (data[i + 5] << 8);
                int height      = data[i + 6] + (data[i + 7] << 8);
                size_t payload  = (size_t)width_bytes * height;
                size_t start    = i + 8;
                if (width_bytes <= 0 || height <= 0 || start + payload > len) {
                    ESP_LOGW(TAG, "raster truncated/invalid"); break;
                }
                EMIT(((draw_op_t){ .kind = OP_RASTER, .raster = {
                    .bits = &data[start], .width_bytes = width_bytes,
                    .width_px = width_bytes * 8, .height = height } }));
                i = start + payload;
            } else if (c == 0x56) { // GS V  cut -> end of job
                ESP_LOGI(TAG, "cut (end of job)");
                break;
            } else {
                ESP_LOGW(TAG, "skip GS 0x%02x", c);
                i += 2;
            }
        } else {
            // Printable/text bytes are ignored in M4a (text is M4c). Skip.
            i++;
        }
    }
    #undef EMIT
    ESP_LOGI(TAG, "parsed %d ops from %u bytes", n, (unsigned)len);
    return n;
}
