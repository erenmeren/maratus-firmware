#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "dev_state.h"

// Pure decision: should the display go to sleep now?
// True iff sleep is enabled, the device is idle, and the inactivity window has
// elapsed. now_ms/last_activity_ms are monotonic milliseconds.
bool should_sleep(uint32_t now_ms, uint32_t last_activity_ms,
                  int timeout_s, bool enabled, dev_state_t state);
