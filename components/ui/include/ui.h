#pragma once

#include <stdbool.h>
#include "device_config.h"

// Ditto on-screen UI (target "B" in the spec): the LVGL screens shown on the
// 720x720 LCD. Mirrors PRINTER_SCREENS in ditto-admin. M2 only renders IDLE;
// later milestones add processing/qr/sent/error/paused/setup.
//
// Thread-safety: all functions here take the LVGL port lock internally, so they
// are safe to call from any task. Call ui_init() once, AFTER the display + LVGL
// port are initialized (e.g. after bsp_display_start()).
typedef enum {
    UI_SCREEN_SETUP,
    UI_SCREEN_IDLE,
    UI_SCREEN_PROCESSING,
    UI_SCREEN_QR,
    UI_SCREEN_SENT,
    UI_SCREEN_ERROR,
    UI_SCREEN_PAUSED,
} ui_screen_t;

// Build the static screens. Call once after the display + LVGL port are up.
void ui_init(void);

// Switch the visible screen.
void ui_show(ui_screen_t screen);

// Set the connectivity status dot on the idle screen (true = online/green).
void ui_set_online(bool online);

// Show the QR screen rendering `url` as a scannable QR code. Thread-safe.
void ui_show_qr(const char *url);

// True (once) if the screen was tapped since the last call. Used to trigger a
// test ingest in M3. Cleared on read.
bool ui_consume_tap(void);

// Rebuild the idle screen from a fetched config (brand background + text objects).
// If cfg is NULL or invalid, the idle screen keeps the built-in default. Thread-safe.
void ui_render_idle(const device_config_t *cfg);
