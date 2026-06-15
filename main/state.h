#pragma once

#include "dev_state.h"

// Start the continuous poll loop (a FreeRTOS task). Returns immediately.
void app_state_run(void);

// Ask the poll loop to re-fetch the device config on its next iteration.
void app_state_request_config(void);
