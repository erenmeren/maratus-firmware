#include "gesture.h"
#include <stdlib.h>

bool gesture_is_swipe_up(int x0, int y0, int x1, int y1, int min_up_px) {
    int up = y0 - y1;              // positive = moved up
    int dx = abs(x1 - x0);
    if (up < min_up_px) return false;
    if (up < dx) return false;     // must be more vertical than horizontal
    return true;
}

snap_decision_t gesture_snap(int revealed_px, int screen_h,
                             int flick_up_velocity,
                             int open_fraction_pct, int flick_threshold) {
    if (flick_up_velocity >= flick_threshold) return SNAP_OPEN;
    if (revealed_px * 100 >= screen_h * open_fraction_pct) return SNAP_OPEN;
    return SNAP_DISMISS;
}
