#pragma once

#include "dev_state.h"

// Start the continuous poll loop (a FreeRTOS task). Returns immediately.
void app_state_run(void);

// Ask the poll loop to re-fetch the device config on its next iteration.
void app_state_request_config(void);

// Publish the current logical screen state. Called by the live trigger handler
// so that a config-changed re-render — which paints
// ui_render_state(s_state) — preserves a live document/QR screen instead of
// clobbering it back to idle. s_state is a single word; access is lock-free.
void app_state_set_state(dev_state_t st);
