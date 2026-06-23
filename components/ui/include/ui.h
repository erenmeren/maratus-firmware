#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "device_config.h"
#include "dev_state.h"
#include "net.h"   // net_ap_t

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

// Stash the pairing code the setup screen's pairingCode widget should display on
// the next render. Pass NULL to clear.
void ui_set_pairing_code(const char *code);

// Set the firmware-chrome connectivity dot (true = online/green). Overlaid on
// every screen; not a branding object.
void ui_set_online(bool online);

// Set the wifi widget signal strength (0..4 bars). Clamped. From live RSSI.
void ui_set_wifi_level(int level);

// Arm the receipt-screen countdown to start at `seconds` on the next render of a
// screen that contains a countdown object. Resets the expired flag.
void ui_set_countdown(int seconds);

// True (once) if the countdown reached 0 since the last call. Cleared on read.
bool ui_consume_countdown_expired(void);

// True (once) if the screen was tapped since the last call. Cleared on read.
bool ui_consume_tap(void);

// True (once) if the screen was touched (pressed) since the last call. Distinct
// from ui_consume_tap (a completed tap): used to detect activity for sleep/wake.
bool ui_consume_activity(void);

// --- Wi-Fi setup screen (M6a-2) -------------------------------------------
// A bespoke, INTERACTIVE screen (not config-object-driven). It is PASSIVE: an
// orchestrator pushes data in (set_results/set_status/prompt_*) and reads user
// intent out via the consume_* flags. Network calls live in the orchestrator,
// never in the LVGL callbacks. All functions take the LVGL port lock internally.

// Build + load the Wi-Fi setup screen.
void ui_wifi_show(void);

// Replace the SSID list with `n` scan results (plus "Other network..." + "Rescan").
void ui_wifi_set_results(const net_ap_t *aps, int n);

// Update the status line at the top of the screen.
void ui_wifi_set_status(const char *text);

// True (once) if the user picked a list row. *idx = result index, or <0 / *manual
// = true for the "Other network..." row. Cleared on read.
bool ui_wifi_consume_selection(int *idx, bool *manual);

// True (once) if the user tapped Connect. Copies the SSID + password the
// orchestrator should use (depends on manual vs picked-network mode). Cleared on read.
bool ui_wifi_consume_connect(char *ssid, int ssid_cap, char *pass, int pass_cap);

// True (once) if the user tapped Rescan. Cleared on read.
bool ui_wifi_consume_rescan(void);

// Put the textarea into manual-SSID entry mode (plain text, "Network name").
void ui_wifi_prompt_ssid(void);

// Put the textarea into password entry mode for `ssid` (masked, "Password").
void ui_wifi_prompt_password(const char *ssid);

// --- Settings screens (M7) ------------------------------------------------
// Two bespoke, PASSIVE screens: a PIN keypad and a device-settings menu.
// An orchestrator pushes data in (set_info/pin_set_error) and reads user
// intent out via the consume_* flags. No network/reboot calls in LVGL
// callbacks. All functions take the LVGL port lock internally.

typedef enum {
    UI_SET_WIFI,        // user tapped "Wi-Fi Setup"
    UI_SET_TEST_PRINT,  // user tapped "Test Print"
    UI_SET_REBOOT,      // user tapped "Reboot"
    UI_SET_CLOSE,       // user tapped "Close" or PIN "Cancel"
} ui_settings_action_t;

// --- Swipe-up gesture (opens full-screen Settings) -------------------------
// Swipe-up is detected on touch release on the active branding screen. The flag
// is set on the LVGL thread; the consumers below take the port lock.

// True (once) if a swipe-up was detected since the last call. Cleared on read.
// Consumed by poll_task to run the settings flow.
bool ui_consume_swipeup(void);

// Non-consuming peek of the swipe-up flag, so the poll loop's wait slices can
// break out early and run the settings flow promptly. Takes the port lock.
bool ui_swipeup_pending(void);

// Suppress new swipe gestures (so a swipe that merely wakes a sleeping screen
// cannot also open settings). Driven by app_state's sleep/wake bookkeeping.
// Takes the port lock.
void ui_set_input_suppressed(bool s);

// The live config pointer (set by ui_set_config); used by ui_settings to color
// itself. Takes the port lock at the call site (it's a plain pointer read).
const device_config_t *ui_current_cfg(void);

// Build the PIN keypad into `parent`. Clears any pending entry and error state.
void ui_settings_show_pin(lv_obj_t *parent);

// Show or hide the "Incorrect PIN" error label. Clears the entry on show.
void ui_settings_pin_set_error(bool show);

// True (once) if the user submitted a PIN. Copies digits into `out` (up to
// `cap-1` chars, NUL-terminated) and clears the buffer. Cleared on read.
bool ui_settings_consume_pin(char *out, int cap);

// Set the device-info block text shown at the top of the menu screen.
void ui_settings_set_info(const char *text);

// Build the menu screen (info label + 4 action buttons) into `parent`.
void ui_settings_show_menu(lv_obj_t *parent);

// True (once) if the user tapped a menu button. Writes the action into `*act`.
// Cleared on read.
bool ui_settings_consume_action(ui_settings_action_t *act);

// Clear the static pointers to the PIN/menu containers WITHOUT deleting them.
// Call after the settings screen is deleted: deleting the parent screen already
// freed those child containers, so this prevents a stale-pointer double-delete
// on the next open.
void ui_settings_reset(void);
