#pragma once
#include <stdbool.h>

// Pure geometry for the styled QR draw (module shapes + finder rings) used by
// components/ui/ui.c's render_qr. No LVGL/qrcodegen dependency, so these are
// host-testable in isolation (tools/cfg-harness).

// QR module/finder shape (config.qrShape from the cloud payload, a sibling of
// clockTimezone/qrTimeoutSeconds inside "config"). ROUNDED is the pre-existing
// look (0.85x circles) and is the default for absent/unknown values, so old
// cloud payloads (field missing) render identically to before this feature.
typedef enum { QR_SHAPE_CLASSIC, QR_SHAPE_SOFT, QR_SHAPE_ROUNDED, QR_SHAPE_DOTS } qr_shape_t;

// Pixel size of one QR module so that `modules + 2*quiet` module-cells fit
// inside `box_side` (the object box's shorter side). Floors down; never
// returns less than 1 (a degenerate 1px module still draws, just illegibly
// small — callers keep the existing `side >= 32` clamp upstream).
int qr_module_px(int box_side, int modules, int quiet);

// Total canvas side in pixels for a given module pixel size — the buffer we
// actually allocate and draw into (modules + quiet zone on both sides).
int qr_canvas_px(int module_px, int modules, int quiet);

// Filled-circle diameter for one dark module: ~0.85x the module cell size,
// clamped to [1, module_px] so it never vanishes or overflows into neighbors.
int qr_dot_diameter_px(int module_px);

// Rounded-rect corner radius for a finder shape (the outer 7-module ring or
// the inner 3-module dot) given that shape's pixel size — ~1/3 of it, per
// spec. Clamped to >= 1.
int qr_finder_radius_px(int shape_px);

// True when module coordinate (mx, my) — 0-based, no quiet zone — falls
// inside any of the three 7x7 finder-pattern blocks (top-left, top-right,
// bottom-left) that every QR Code symbol of the given module count has at
// fixed corners. Modules inside a finder block are skipped by the per-module
// dot draw; the finder is instead drawn once as two rounded rects.
bool qr_in_finder_block(int modules, int mx, int my);

// Dot diameter for the "dots" shape: ~0.7x the module cell size, clamped to
// [1, module_px] (tighter than qr_dot_diameter_px's 0.85x "rounded" look).
int qr_dots_diameter_px(int module_px);

// Corner radius for the "soft" shape's rounded-square modules: ~0.25x the
// module cell size, clamped to [1, module_px/2] so opposite corners never
// overlap.
int qr_soft_radius_px(int module_px);

// Finder-ring corner radius for a given shape: 0 (square corners) for
// QR_SHAPE_CLASSIC, otherwise the existing rounded-ring radius
// (qr_finder_radius_px) for soft/rounded/dots.
int qr_finder_radius_px_for(qr_shape_t shape, int shape_px);

// What/how to draw one dark QR module (outside the finder blocks) for a given
// shape, at a given module cell size in px. `kind` selects the LVGL draw
// strategy the caller applies; `size` is the drawn shape's side (square/
// rounded-square) or diameter (circle); `offset` centers it within the
// module_px cell; `radius` is the corner radius (rounded-square only —
// circles use LVGL's LV_RADIUS_CIRCLE directly, squares use 0).
typedef enum { QR_DRAW_SQUARE, QR_DRAW_ROUNDED_SQUARE, QR_DRAW_CIRCLE } qr_draw_kind_t;

typedef struct {
    qr_draw_kind_t kind;
    int size;
    int offset;
    int radius;
} qr_module_style_t;

qr_module_style_t qr_module_style(qr_shape_t shape, int module_px);

// QR background-plate corner treatment (config.qrCorner, a sibling of
// qrShape/qrFg/qrBg/qrShadow inside "config"). ROUNDED is the default for
// absent/unknown values (mirrors the cloud's DEFAULT_QR_STYLE.qrCorner in
// ditto-admin lib/printer-layout.ts).
typedef enum { QR_CORNER_SQUARE, QR_CORNER_ROUNDED } qr_corner_t;

// Corner radius, in canvas px, for the QR background plate: ~6% of
// canvas_px when ROUNDED (matches the cloud's lib/qr-svg.ts
// qrBackgroundRadius(dim, "rounded") = dim * 0.06), 0 (square) otherwise.
// Applied as the QR canvas lv_obj's own `radius` style — LVGL clips an
// image/canvas widget's own pixel content to that rounded rect at composite
// time (lv_image.c's draw_dsc.clip_radius), so no pixel-format/alpha change
// is needed to get "transparent" corners: whatever sits behind the canvas
// (the screen background) simply shows through the clipped-away corners.
int qr_bg_radius_px(int canvas_px, qr_corner_t corner);

// QR drop-shadow mode (config.qrShadowMode, a sibling of qrCorner/qrShape/
// qrFg/qrBg inside "config"). NONE draws no shadow at all (default for
// absent/unknown). DROP is a soft shadow behind-and-below the plate
// (today's fw 0.12.0 look, now strength/color tunable). NEON is a fuller,
// brighter surrounding glow. Older cloud payloads sent a plain boolean
// `qrShadow`; cfg_parse.c migrates qrShadow==true to DROP only when
// qrShadowMode itself is absent from the payload.
typedef enum { QR_SHADOW_NONE, QR_SHADOW_DROP, QR_SHADOW_NEON } qr_shadow_mode_t;
