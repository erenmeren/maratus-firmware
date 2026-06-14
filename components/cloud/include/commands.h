#pragma once

// Parse a {"commands":[{"id","type"}]} body and act on each command, acking each.
// Supported: identify (blink status dot), reboot (esp_restart — does not return).
// Unknown / refresh / config-changed are acked ok=true and otherwise ignored in M2.
// Callers that touch LVGL (identify) must NOT hold the LVGL lock here — the
// handler takes it internally as needed.
void commands_handle_body(const char *json_body);
