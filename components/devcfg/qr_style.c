#include "qr_style.h"

int qr_module_px(int box_side, int modules, int quiet) {
    if (modules <= 0) return 0;
    int total = modules + 2 * quiet;
    if (total <= 0) return 0;
    int px = box_side / total;
    return px < 1 ? 1 : px;
}

int qr_canvas_px(int module_px, int modules, int quiet) {
    if (module_px < 0) return 0;
    return module_px * (modules + 2 * quiet);
}

int qr_dot_diameter_px(int module_px) {
    if (module_px < 1) return 1;   // never vanishes even for a degenerate cell
    int d = (int)((float)module_px * 0.85f + 0.5f);
    if (d < 1) d = 1;
    if (d > module_px) d = module_px;
    return d;
}

int qr_finder_radius_px(int shape_px) {
    int r = shape_px / 3;
    return r < 1 ? 1 : r;
}

static bool in_block(int mx, int my, int bx, int by) {
    return mx >= bx && mx < bx + 7 && my >= by && my < by + 7;
}

bool qr_in_finder_block(int modules, int mx, int my) {
    if (modules < 7) return false;
    return in_block(mx, my, 0, 0) ||               // top-left
           in_block(mx, my, modules - 7, 0) ||      // top-right
           in_block(mx, my, 0, modules - 7);        // bottom-left
}

int qr_dots_diameter_px(int module_px) {
    if (module_px < 1) return 1;   // never vanishes even for a degenerate cell
    int d = (int)((float)module_px * 0.7f + 0.5f);
    if (d < 1) d = 1;
    if (d > module_px) d = module_px;
    return d;
}

int qr_soft_radius_px(int module_px) {
    if (module_px < 1) return 1;
    int r = (int)((float)module_px * 0.25f + 0.5f);
    if (r < 1) r = 1;
    int max_r = module_px / 2;
    if (max_r < 1) max_r = 1;
    if (r > max_r) r = max_r;
    return r;
}

int qr_finder_radius_px_for(qr_shape_t shape, int shape_px) {
    if (shape == QR_SHAPE_CLASSIC) return 0;   // square corners
    return qr_finder_radius_px(shape_px);
}

qr_module_style_t qr_module_style(qr_shape_t shape, int module_px) {
    qr_module_style_t s = { QR_DRAW_CIRCLE, 0, 0, 0 };
    switch (shape) {
    case QR_SHAPE_CLASSIC:
        s.kind = QR_DRAW_SQUARE;
        s.size = module_px;
        break;
    case QR_SHAPE_SOFT:
        s.kind = QR_DRAW_ROUNDED_SQUARE;
        s.size = module_px;
        s.radius = qr_soft_radius_px(module_px);
        break;
    case QR_SHAPE_DOTS:
        s.kind = QR_DRAW_CIRCLE;
        s.size = qr_dots_diameter_px(module_px);
        break;
    case QR_SHAPE_ROUNDED:
    default:
        s.kind = QR_DRAW_CIRCLE;
        s.size = qr_dot_diameter_px(module_px);
        break;
    }
    s.offset = (module_px - s.size) / 2;
    if (s.offset < 0) s.offset = 0;
    return s;
}

int qr_bg_radius_px(int canvas_px, qr_corner_t corner) {
    if (corner != QR_CORNER_ROUNDED) return 0;
    if (canvas_px < 1) return 0;
    int r = (int)((float)canvas_px * 0.06f + 0.5f);
    return r < 0 ? 0 : r;
}

static int clamp_strength(int strength) {
    if (strength < 0)   return 0;
    if (strength > 100) return 100;
    return strength;
}

int qr_shadow_width_px(int strength) {
    strength = clamp_strength(strength);
    // 8 .. 48 px, rounded to the nearest px (+50 before the /100 truncation
    // is the integer round-half-up idiom used throughout this file).
    return 8 + (strength * 40 + 50) / 100;
}

int qr_shadow_opa_pct(int strength, qr_shadow_mode_t mode) {
    strength = clamp_strength(strength);
    if (mode == QR_SHADOW_NEON) {
        return 35 + (strength * 65 + 50) / 100;   // 35 .. 100
    }
    return 20 + (strength * 70 + 50) / 100;        // DROP (and defensively NONE): 20 .. 90
}
