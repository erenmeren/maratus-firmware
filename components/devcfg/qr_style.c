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

// CLOUD PARITY: mirrors ditto-admin lib/qr-svg.ts qrCornerRadiusPx — keep in sync.
int qr_corner_radius_px(int dim, int v) {
    if (dim < 1) return 0;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    // dim * 0.30 * v/100 == dim*3*v/1000; +500 before the /1000 truncation
    // is the integer round-half-up idiom used throughout this file.
    long num = (long)dim * 3L * (long)v;
    return (int)((num + 500) / 1000);
}

static int clamp_strength(int strength) {
    if (strength < 0)   return 0;
    if (strength > 100) return 100;
    return strength;
}

// CLOUD PARITY: mirrors ditto-admin lib/qr-svg.ts qrShadowParams — keep in sync.
qr_shadow_params_t qr_shadow_params(qr_shadow_mode_t mode, int strength, int dim) {
    qr_shadow_params_t p = { 0, 0, 0, 0 };
    if (mode == QR_SHADOW_NONE) return p;
    if (dim < 1) return p;   // degenerate canvas -> no shadow, never crash
    strength = clamp_strength(strength);

    // blurPx = dim * (0.04 + 0.20 * strength/100) == dim*(40 + 2*strength)/1000.
    long blur_num = (long)dim * (40L + 2L * strength);
    p.blur_px = (int)((blur_num + 500) / 1000);

    if (mode == QR_SHADOW_DROP) {
        // offsetYPx = dim * 0.02 == dim*2/100.
        p.offset_y_px = (int)(((long)dim * 2L + 50) / 100);
        // alpha = 0.25 + 0.50*s (s = strength/100); opa (0..255) =
        // round(alpha*255) == round((250 + 5*strength) * 255 / 1000).
        long opa_num = (250L + 5L * strength) * 255L;
        p.opa = (int)((opa_num + 500) / 1000);
        p.spread_px = 0;
        return p;
    }

    // NEON: full-color halo (alpha 1.0 -> opa 255 exactly), no downward
    // offset. spread = 2x the UNROUNDED blur (computed directly from dim/
    // strength, not from the already-rounded blur_px) so it matches the
    // cloud's float math exactly rather than drifting from double rounding.
    p.offset_y_px = 0;
    p.opa = 255;
    long spread_num = 2L * (long)dim * (40L + 2L * strength);
    p.spread_px = (int)((spread_num + 500) / 1000);
    return p;
}
