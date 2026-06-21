// Host shim: map ESP PSRAM allocator to plain malloc for off-device tests.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
static inline void *heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
