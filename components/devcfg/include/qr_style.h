#pragma once
#include <stdbool.h>

// Pure geometry for the styled QR draw (rounded dark-module circles + rounded
// finder rings) used by components/ui/ui.c's render_qr. No LVGL/qrcodegen
// dependency, so these are host-testable in isolation (tools/cfg-harness).

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
