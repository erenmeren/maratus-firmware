#pragma once

// Device lifecycle states (firmware spec §6). Lives in devcfg so both the
// ui component and main can include it without depending on main/.
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
