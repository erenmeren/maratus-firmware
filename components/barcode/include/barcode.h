#pragma once
#include <stddef.h>

// Encode `data` (len bytes, treated as Code128-B / ASCII 32..127) into a run of
// modules: out[k] = 1 for a bar, 0 for a space (no quiet zone — caller adds it).
// Returns the module count, or -1 on overflow/empty. cap = capacity of `out`.
int code128b_encode(const char *data, int len, unsigned char *out, int cap);
