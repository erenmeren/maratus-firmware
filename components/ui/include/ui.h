#pragma once

#include <stdbool.h>
#include "device_config.h"
#include "dev_state.h"

// Ditto on-screen UI: a generic renderer that builds the active branding screen
// from the server config. Mirrors PRINTER_SCREENS in ditto-admin.
//
// Thread-safety: all functions take the LVGL port lock internally; safe from any
// task. Call ui_init() once AFTER the display + LVGL port are up.

// Build base state. Call once after the display + LVGL port are initialized.
void ui_init(void);

// Point the renderer at the live config (stores the pointer; cfg must outlive UI).
void ui_set_config(const device_config_t *cfg);

// Render the branding screen mapped from a device state. Falls back to a built-in
// splash for DEV_BOOT or when no valid config is set.
void ui_render_state(dev_state_t st);

// Stash the URL the qr widget should encode on the next/ current qr render.
void ui_set_qr_url(const char *url);

// Set the firmware-chrome connectivity dot (true = online/green). Overlaid on
// every screen; not a branding object.
void ui_set_online(bool online);

// True (once) if the screen was tapped since the last call. Cleared on read.
bool ui_consume_tap(void);
