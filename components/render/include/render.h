#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "draw_ops.h"

// Set the framebuffer/roll width in dots (default RENDER_ROLL_WIDTH). Call once at boot.
void render_set_roll_width(int dots);

// Render a list of draw-ops to a PNG. Composes a grayscale framebuffer (width =
// configured roll width, height grown from the ops), then PNG-encodes it.
// Allocates *png_out (caller frees). Returns true + sets dims on success.
bool render_ops_to_png(const draw_op_t *ops, int n_ops,
                       uint8_t **png_out, size_t *png_len,
                       int *out_w, int *out_h);
