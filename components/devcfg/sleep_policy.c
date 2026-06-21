#include "sleep_policy.h"

bool should_sleep(uint32_t now_ms, uint32_t last_activity_ms,
                  int timeout_s, bool enabled, dev_state_t state) {
    if (!enabled) return false;
    if (state != DEV_IDLE) return false;
    if (timeout_s < 1) timeout_s = 1;
    uint32_t elapsed = now_ms - last_activity_ms;   // monotonic; wrap is harmless here
    return elapsed >= (uint32_t)timeout_s * 1000u;
}
