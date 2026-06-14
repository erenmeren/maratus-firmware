#pragma once
#include <stddef.h>
#include <stdint.h>
#include "draw_ops.h"

// Parse an Epson ESC/POS byte stream into draw-ops (M4a subset: ESC @, ESC a,
// GS v 0 raster, LF/ESC J/ESC d feeds, GS V cut). Raster ops point INTO `data`
// (which must outlive use of the ops). Writes up to `max_ops` into `ops`.
// Returns the number of ops produced.
int escpos_parse(const uint8_t *data, size_t len, draw_op_t *ops, int max_ops);
