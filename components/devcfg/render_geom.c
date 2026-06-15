#include "render_geom.h"

int geom_frac_to_px(float frac) {
    int px = (int)(frac * (float)UI_CANVAS_PX + 0.5f);
    if (px < 0) return 0;
    if (px > UI_CANVAS_PX) return UI_CANVAS_PX;
    return px;
}

px_box_t geom_box(const cfg_object_t *o) {
    px_box_t b;
    b.x = geom_frac_to_px(o->x);
    b.y = geom_frac_to_px(o->y);
    b.w = geom_frac_to_px(o->w);
    b.h = geom_frac_to_px(o->h);
    return b;
}

void cfg_sort_by_z(const cfg_object_t *objs, int n, int *order) {
    for (int i = 0; i < n; i++) order[i] = i;
    // Stable insertion sort by z (n is tiny: <= CFG_MAX_OBJECTS).
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && objs[order[j]].z > objs[key].z) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

dev_screen_t screen_for_state(dev_state_t st) {
    switch (st) {
        case DEV_SETUP:      return SCREEN_SETUP;
        case DEV_PROCESSING: return SCREEN_PROCESSING;
        case DEV_UPLOADING:  return SCREEN_PROCESSING;
        case DEV_QR:         return SCREEN_QR;
        case DEV_SENT:       return SCREEN_SENT;
        case DEV_ERROR:      return SCREEN_ERROR;
        case DEV_PAUSED:     return SCREEN_PAUSED;
        case DEV_IDLE:       return SCREEN_IDLE;
        case DEV_BOOT:       default: return SCREEN_IDLE;
    }
}
