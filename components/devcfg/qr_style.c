#include "qr_style.h"
#include <math.h>

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

// CLOUD PARITY: mirrors maratus-admin lib/qr-svg.ts qrCornerRadiusPx — keep in sync.
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

// CLOUD PARITY: mirrors maratus-admin lib/qr-svg.ts qrShadowParams — keep in sync.
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

// --- NEON pre-rendered canvas glow (fw 0.16.0) — see qr_style.h for the
// full "why pre-render instead of a live LVGL shadow" rationale. ---

int qr_neon_halo_px(int plate_px, int spread_px, int max_side_px) {
    if (plate_px < 1 || spread_px < 0) return 0;
    int halo = spread_px > plate_px ? plate_px : spread_px;
    int max_halo = (max_side_px - plate_px) / 2;
    if (max_halo < 0) max_halo = 0;
    if (halo > max_halo) halo = max_halo;
    return halo;
}

int qr_glow_dist_px(int px, int py, int rect_w, int rect_h, int r) {
    if (rect_w < 1 || rect_h < 1) return 0;
    if (r < 0) r = 0;
    int max_r = (rect_w < rect_h ? rect_w : rect_h) / 2;
    if (r > max_r) r = max_r;

    float half_w = (float)rect_w / 2.0f;
    float half_h = (float)rect_h / 2.0f;
    // Inigo Quilez's sdRoundBox, evaluated directly in the plate's own
    // pixel coordinates (no recenter-then-shift needed since we work in
    // "distance from center, minus the rounded half-extent" terms below).
    float qx = fabsf((float)px - half_w) - (half_w - (float)r);
    float qy = fabsf((float)py - half_h) - (half_h - (float)r);
    float mx = qx > 0.0f ? qx : 0.0f;
    float my = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(mx * mx + my * my);
    float inside = qx > qy ? qx : qy;
    if (inside > 0.0f) inside = 0.0f;
    float d = outside + inside - (float)r;
    if (d < 0.0f) d = 0.0f;
    return (int)(d + 0.5f);
}

int qr_glow_alpha_at(int d, int blur_px, int spread_px) {
    if (d <= 0) return 255;
    float a1 = 0.0f, a2 = 0.0f;
    if (blur_px > 0 && d < blur_px) {
        float t = 1.0f - (float)d / (float)blur_px;
        a1 = t * t;
    }
    if (spread_px > 0 && d < spread_px) {
        float t = 1.0f - (float)d / (float)spread_px;
        a2 = 0.55f * t * t;
    }
    float a = a1 > a2 ? a1 : a2;
    if (a <= 0.0f) return 0;
    if (a > 1.0f) a = 1.0f;
    int alpha = (int)(a * 255.0f + 0.5f);
    if (alpha > 255) alpha = 255;
    if (alpha < 0)   alpha = 0;
    return alpha;
}
