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

// QR background-plate corner radius (config.qrCornerRadius, a sibling of
// qrShape/qrFg/qrBg/qrShadow inside "config"): a continuous 0..100 slider
// (0 = square), replacing the earlier 2-value `qrCorner: "square" |
// "rounded"` enum (2026-07-24 slider addendum).
//
// CLOUD PARITY: mirrors ditto-admin lib/qr-svg.ts qrCornerRadiusPx — keep in sync.
//
// Corner radius, in px, for a background plate of pixel dimension `dim`:
// round(dim * 0.30 * v/100) — 0 at v=0 (square), scaling linearly to 30% of
// `dim` at v=100 (a soft, pill-ish rounding). Applied as the QR canvas
// lv_obj's own `radius` style — LVGL clips an image/canvas widget's own
// pixel content to that rounded rect at composite time (lv_image.c's
// draw_dsc.clip_radius), so no pixel-format/alpha change is needed to get
// "transparent" corners: whatever sits behind the canvas (the screen
// background) simply shows through the clipped-away corners. Int math,
// round-half-up; dim < 1 -> 0 (degenerate canvas, never crashes).
int qr_corner_radius_px(int dim, int v);

// QR drop-shadow mode (config.qrShadowMode, a sibling of qrCorner/qrShape/
// qrFg/qrBg inside "config"). NONE draws no shadow at all (default for
// absent/unknown). DROP is a soft shadow behind-and-below the plate
// (today's fw 0.12.0 look, now strength/color tunable). NEON is a fuller,
// brighter surrounding glow. Older cloud payloads sent a plain boolean
// `qrShadow`; cfg_parse.c migrates qrShadow==true to DROP only when
// qrShadowMode itself is absent from the payload.
typedef enum { QR_SHADOW_NONE, QR_SHADOW_DROP, QR_SHADOW_NEON } qr_shadow_mode_t;

// CLOUD PARITY: mirrors ditto-admin lib/qr-svg.ts qrShadowParams — keep in sync.
//
// Canonical dim-relative shadow/glow numbers for the QR background plate, as
// a function of the plate's OWN pixel dimension `dim` (the canvas side) —
// every number is `dim`-relative (not a fixed px constant) so the effect
// reads the same *proportional* size regardless of how large the QR is
// rendered (this dim-relative math is the fix for "the shadow looks
// different on-device than in the studio preview" — before this addendum,
// blur/offset were fixed px constants independent of the plate's actual
// rendered size).
//
// All px/opa fields are rounded from the underlying float math independently
// (not chained through an already-rounded intermediate) so e.g. spread_px is
// exactly 2x the UNROUNDED blur, matching the cloud's qrShadowParams.
typedef struct {
    int blur_px;      // 0 for NONE; else dim * (0.04 + 0.20 * strength/100)
    int offset_y_px;  // DROP: dim * 0.02; NEON/NONE: 0
    int opa;           // 0..255 shadow-color opacity. DROP: (0.25 + 0.50*s)*255;
                        // NEON: 255 (full-color halo, not just a soft shadow); NONE: 0
    int spread_px;      // NEON only: 2x the unrounded blur (a wider second pass,
                         // the outer halo); 0 for DROP/NONE
} qr_shadow_params_t;

// strength (0..100) is re-clamped here defensively even though cfg_parse.c
// already clamps it. Int math, round-half-up throughout.
qr_shadow_params_t qr_shadow_params(qr_shadow_mode_t mode, int strength, int dim);
