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
// CLOUD PARITY: mirrors maratus-admin lib/qr-svg.ts qrCornerRadiusPx — keep in sync.
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

// CLOUD PARITY: mirrors maratus-admin lib/qr-svg.ts qrShadowParams — keep in sync.
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

// --- NEON pre-rendered canvas glow (fw 0.16.0) ---
//
// LVGL's live `shadow_width`/`shadow_spread` box-shadow is hard-capped at
// 64/16px (components/ui/ui.c QR_SHADOW_WIDTH_MAX/QR_SHADOW_SPREAD_MAX) —
// raising that cap wedges the software shadow blur for seconds per frame
// and starves poll_task (the 0.14.0 boot-hang incident; see BUILD.md's
// 0.15.0 fix). So NEON no longer uses a live LVGL shadow at all: the glow
// is pre-rendered ONCE into the QR canvas's own (enlarged) pixel buffer at
// canvas-build time — zero per-frame cost, and it can match the cloud
// preview's full halo reach without touching the render-loop budget.

// Absolute cap on the enlarged NEON canvas's side, in px — independent of
// how large `spreadPx` (qr_shadow_params's cloud halo reach) computes to.
// 720 is the panel's own side; a QR glow canvas can never usefully need to
// be larger than the whole screen.
#define QR_NEON_CANVAS_SIDE_MAX_PX 720

// Outward glow reach (added on all 4 sides of the plate) for the enlarged
// NEON canvas, in px. `spread_px` is the cloud-parity halo reach from
// qr_shadow_params(QR_SHADOW_NEON, strength, plate_px).spread_px — this
// function only applies device-side sanity caps on top of that CLOUD
// PARITY number, not its own parity math: (1) the halo can never exceed
// the plate's own size (a halo bigger than the plate reads the same as one
// capped to it, and bounds worst-case PSRAM/loop cost), and (2) the final
// canvas side (plate + 2*halo) is hard-capped at `max_side_px`
// (QR_NEON_CANVAS_SIDE_MAX_PX in practice). Returns 0 (no glow, degenerate
// canvas) for plate_px < 1; never negative.
int qr_neon_halo_px(int plate_px, int spread_px, int max_side_px);

// CLOUD PARITY: distance in px from point (px, py) — in the plate's own
// pixel coordinate space, (0,0) = the plate's top-left corner — to OUTSIDE
// a rounded rect of size rect_w x rect_h with corner radius r. 0 if the
// point is inside or on the boundary. This is the standard rounded-box
// signed-distance-field (Inigo Quilez's sdRoundBox: q = |p - center| -
// (halfExtent - r); d = length(max(q,0)) + min(max(q.x,q.y),0) - r),
// clamped to >= 0 here since qr_glow_alpha_at only needs the OUTSIDE
// distance. r is clamped to <= min(rect_w,rect_h)/2 (opposite corners
// never overlap), matching qr_soft_radius_px's convention. Feeds the
// neon-glow falloff so the halo follows the plate's own corner rounding
// (qr_corner_radius_px) instead of a plain rectangle.
int qr_glow_dist_px(int px, int py, int rect_w, int rect_h, int r);

// CLOUD PARITY: approximates the branding studio preview's two-layer glow
// (a tight inner bloom stacked under a wider, dimmer outer bloom — the
// preview's stacked CSS box-shadow/blur passes) with two closed-form
// falloffs evaluated at distance `d` (px, from qr_glow_dist_px) outside the
// plate:
//   a1 = (1 - d/blur_px)^2         for d < blur_px   (tight inner glow)
//   a2 = 0.55 * (1 - d/spread_px)^2 for d < spread_px  (wide outer glow,
//                                     capped at 55% peak so it reads as a
//                                     dimmer halo, not a second bright ring)
// alpha = round(255 * max(a1, a2)) — the brighter of the two layers wins at
// each pixel, mirroring how stacked box-shadows composite. d <= 0 (inside
// the plate) short-circuits to 255. blur_px/spread_px <= 0 disable their
// respective term (no div-by-zero). Pure int-in/int-out; the float math
// inside only runs once per pixel at canvas-build time (boot-time, not
// per-frame), so it's not a render-loop cost.
int qr_glow_alpha_at(int d, int blur_px, int spread_px);
