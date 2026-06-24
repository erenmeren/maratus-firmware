#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "dev_state.h"

#define CFG_MAX_OBJECTS 24    // per screen; admin MAX_CUSTOM(20) + widget headroom
#define CFG_TEXT_LEN    80    // matches MAX_TEXT_LEN in ditto-admin printer-layout
#define CFG_PRESET_LEN  24
#define CFG_TZ_LEN      64
#define CFG_URL_LEN     768   // must hold a full presigned R2 URL (~500 chars + headroom);
                              // config lives in PSRAM so the per-object cost is fine

// Mirrors PRINTER_SCREENS in ditto-admin. Order is the array index — do not reorder.
typedef enum {
    SCREEN_IDLE, SCREEN_PROCESSING, SCREEN_QR, SCREEN_SENT,
    SCREEN_ERROR, SCREEN_PAUSED, SCREEN_SETUP, SCREEN_COUNT
} dev_screen_t;

typedef enum {
    OBJ_TEXT, OBJ_ICON,
    OBJ_LOGO, OBJ_CLOCK, OBJ_WIFI, OBJ_QR, OBJ_SPINNER,
    OBJ_COUNTDOWN, OBJ_PAIRING_CODE, OBJ_STEPS,
    OBJ_IMAGE,
    OBJ_UNKNOWN
} cfg_obj_type_t;

typedef enum { CFG_ALIGN_LEFT, CFG_ALIGN_CENTER, CFG_ALIGN_RIGHT } cfg_align_t;
typedef enum { ICON_PRESET, ICON_UPLOAD } cfg_icon_src_t;
typedef enum { TINT_ACCENT, TINT_MUTED, TINT_WARN, TINT_NONE } cfg_tint_t;

typedef struct {
    cfg_obj_type_t type;
    float x, y, w, h;          // top-left + size, fractions 0..1 on the 720 reference
    bool  visible;
    int   z;
    // text
    char  text[CFG_TEXT_LEN];
    int   font_size;           // px on the 720 reference
    cfg_align_t align;
    // icon
    cfg_icon_src_t icon_src;
    char  icon_preset[CFG_PRESET_LEN];
    char  icon_url[CFG_URL_LEN];   // presigned (rotates); cache-key derived from path in M5b
    cfg_tint_t icon_tint;
    bool  icon_circle;
    // image
    char  image_url[CFG_URL_LEN];   // presigned (rotates); from image.signedUrl
    // clock
    bool  clock_show_date;
    bool  clock_show_weekday;
} cfg_object_t;

typedef struct {
    cfg_object_t objects[CFG_MAX_OBJECTS];
    int n;
} cfg_screen_t;

typedef struct {
    bool     valid;            // false until a config has been parsed
    char     etag[48];         // payload "version" hex (used as If-None-Match)
    uint32_t brand_bg;         // 0xRRGGBB
    uint32_t brand_fg;
    uint32_t brand_accent;
    uint32_t brand_muted;
    uint32_t brand_warn;       // M5a parses with a default; admin sends brandWarn in M5b
    char     logo_url[CFG_URL_LEN];
    char     wordmark[CFG_TEXT_LEN];   // brand wordmark text for the logo widget
    char     clock_timezone[CFG_TZ_LEN];
    bool     clock_24h;
    int      wifi_level;       // 0..4
    int      qr_timeout_seconds;  // receipt/QR auto-return timeout, 15..180
    // M7 org-wide device policies (from the payload "device" block).
    struct {
        int  brightness;                  // 10..100
        struct { bool enabled; int timeout_seconds; } sleep;  // 30..3600
        char settings_password_hash[65];  // sha256(salt+pin) hex; "" = unset
        char settings_password_salt[33];  // "" = unset
    } device;
    cfg_screen_t screens[SCREEN_COUNT];
} device_config_t;
