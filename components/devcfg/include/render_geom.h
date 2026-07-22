#pragma once
#include "device_config.h"

#define UI_CANVAS_PX 720   // the 720x720 physical reference

typedef struct { int x, y, w, h; } px_box_t;

// round(frac * 720), clamped to [0, 720].
int geom_frac_to_px(float frac);

// Convert an object's fractional box to a pixel rect.
px_box_t geom_box(const cfg_object_t *o);

// Fill order[0..n) with object indices sorted ascending by z, stable on ties.
void cfg_sort_by_z(const cfg_object_t *objs, int n, int *order);

// Map a device lifecycle state to the branding screen it renders.
dev_screen_t screen_for_state(dev_state_t st);

// Which lifecycle state "go home" lands on: DEV_PINNED when a pin is set
// (config's `pin` block), otherwise the plain idle screen.
dev_state_t home_state(bool pin_set);
