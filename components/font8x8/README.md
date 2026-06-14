# font8x8 (vendored)

8x8 monospace bitmap font (basic Latin, ASCII 0–127) by Daniel Hepper — **public domain**.
Source: https://github.com/dhepper/font8x8 (`font8x8_basic.h`).

`char font8x8_basic[128][8]` — each glyph is 8 row-bytes; bit 0 (LSB) is the leftmost
column. Used by `components/render` for ESC/POS text (M4c).
