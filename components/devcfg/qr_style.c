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
