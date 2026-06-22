#pragma once
#include <stdbool.h>

// Pure decision: did a touch travel from press-point (x0,y0) to release-point
// (x1,y1) constitute an upward swipe? Upward = y decreasing (origin top-left).
// True iff the upward travel (y0 - y1) is at least min_up_px AND the gesture is
// more vertical than horizontal (upward travel >= absolute horizontal travel).
bool gesture_is_swipe_up(int x0, int y0, int x1, int y1, int min_up_px);
