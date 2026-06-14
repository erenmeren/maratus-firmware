#pragma once

// Device lifecycle states (firmware spec §6). M2 lives in IDLE; the
// receipt/upload/qr transitions arrive in M3-M4, provisioning/paused in M5-M6.
typedef enum {
    DEV_BOOT,
    DEV_SETUP,
    DEV_IDLE,
    DEV_PROCESSING,
    DEV_UPLOADING,
    DEV_QR,
    DEV_SENT,
    DEV_ERROR,
    DEV_PAUSED,
} dev_state_t;

// Start the continuous poll loop (a FreeRTOS task). Returns immediately.
void app_state_run(void);

// Ask the poll loop to re-fetch the device config on its next iteration.
void app_state_request_config(void);
