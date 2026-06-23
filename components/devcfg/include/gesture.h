#pragma once
#include <stdbool.h>

// Pure decision: did a touch travel from press-point (x0,y0) to release-point
// (x1,y1) constitute an upward swipe? Upward = y decreasing (origin top-left).
// True iff the upward travel (y0 - y1) is at least min_up_px AND the gesture is
// more vertical than horizontal (upward travel >= absolute horizontal travel).
bool gesture_is_swipe_up(int x0, int y0, int x1, int y1, int min_up_px);

typedef enum { SNAP_DISMISS = 0, SNAP_OPEN = 1 } snap_decision_t;

// Pure decision: should a swipe-up reveal of a settings sheet open or dismiss?
// revealed_px: how far the sheet was pulled up from the bottom (0..screen_h).
// flick_up_velocity: upward release velocity in px/s (downward clamps to 0 by caller).
// open_fraction_pct: % of screen_h that auto-opens (e.g. 40).
// flick_threshold: px/s upward flick that opens regardless of distance.
snap_decision_t gesture_snap(int revealed_px, int screen_h,
                             int flick_up_velocity,
                             int open_fraction_pct, int flick_threshold);
