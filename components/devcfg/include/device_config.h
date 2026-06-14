#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CFG_MAX_TEXTS 32
#define CFG_TEXT_LEN  80   // matches MAX_TEXT_LEN in ditto-admin printer-layout

typedef struct {
    bool  visible;
    float x, y;          // top-left, fraction 0..1 on the 720 reference
    char  text[CFG_TEXT_LEN];
    int   font_size;     // px on the 720 reference
    int   align;         // 0 left, 1 center, 2 right
} cfg_text_t;

typedef struct {
    bool     valid;            // false until a config has been parsed
    char     etag[48];         // payload "version" hex (used as If-None-Match)
    uint32_t brand_bg;         // 0xRRGGBB
    uint32_t brand_fg;         // 0xRRGGBB (default text color)
    cfg_text_t texts[CFG_MAX_TEXTS];
    int      n_texts;          // text objects on the idle screen
} device_config_t;
