#include "gesture.h"
#include <stdlib.h>

bool gesture_is_swipe_up(int x0, int y0, int x1, int y1, int min_up_px) {
    int up = y0 - y1;              // positive = moved up
    int dx = abs(x1 - x0);
    if (up < min_up_px) return false;
    if (up < dx) return false;     // must be more vertical than horizontal
    return true;
}
