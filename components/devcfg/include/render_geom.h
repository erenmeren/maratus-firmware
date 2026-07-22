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

// Map a device lifecycle state to the branding screen it renders. Cfg-unaware:
// DEV_PINNED always maps to SCREEN_QR here (the safe fallback layout) — callers
// that have a live config and want the editable pinned screen should use
// screen_for_pinned() instead for that one state.
dev_screen_t screen_for_state(dev_state_t st);

// Which branding screen a DEV_PINNED render should use: the dedicated
// SCREEN_PINNED layout when the live config gives it at least one object,
// otherwise the SCREEN_QR fallback (old cached configs with no "pinned" key
// parse to an empty screen, same as a screen the tenant never edited).
dev_screen_t screen_for_pinned(const device_config_t *cfg);

// Which lifecycle state "go home" lands on: DEV_PINNED when a pin is set
// (config's `pin` block), otherwise the plain idle screen.
dev_state_t home_state(bool pin_set);
