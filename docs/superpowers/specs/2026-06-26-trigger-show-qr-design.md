# Firmware Trigger Execution — `show_qr` + ack (Spec B) — Design

**Date:** 2026-06-26
**Repo:** `ditto-firmware` (device only — no cloud changes)
**Status:** Approved design, pre-implementation
**Part of:** the public credit-gated device-trigger feature. **Spec A** (cloud) shipped to prod 2026-06-26 (see ditto-admin `docs/superpowers/specs/2026-06-26-credits-and-device-trigger-core-design.md`). This is **Spec B** — the device half that makes a trigger actually do something and **acks** it (which settles the credit hold in the cloud).

## Problem

The cloud enqueues a `trigger` command (`{ type:"trigger", action:"show_qr", payload:{ url } }`) to a device, having **reserved** a credit hold. The device currently parses only `id`/`type` and ignores `action`/`payload`; it never handles `trigger`, never shows the URL, and never acks it. So today a trigger holds a credit that only ever **releases** on expiry (never settles) — the feature does nothing on hardware.

## Goal

On receiving a `trigger`/`show_qr` command, the device displays a QR of `payload.url` on the existing receipt-QR screen (auto-returning after `qrTimeoutSeconds`) and **acks** it — `ok=true` once displayed (cloud **settles** = charged), `ok=false` for an invalid/missing url (cloud **releases** = refund).

## Decisions (locked via brainstorming)

1. **Reuse the existing `DEV_QR` screen** (same layout + `qrTimeoutSeconds` countdown + auto-return). No new screen state.
2. **Charge on display:** ack `ok=true` once the QR is shown (we can't know if the customer scanned). Invalid/missing url → ack `ok=false`.
3. **Cloud is unchanged:** Spec A already sends `action`+`payload` and settles/releases on ack. This is firmware-only.

## Key facts (what already exists — verified)

- Command poll + dispatch: `components/cloud/commands.c` `commands_handle_body()` parses `{commands:[{id,type}]}` and switches on `type` (identify/reboot/config-changed/firmware-update). `action`/`payload` are NOT parsed yet.
- Ack: `components/cloud/cloud.c` `cloud_ack_command(id, ok)` → `POST /api/device/commands/ack {commandId, ok}`. Fully working; used by every handler.
- QR content setter: `components/ui/ui.c` `ui_set_qr_url(url)` (thread-safe, LVGL-lock, 256-byte `s_qr_url`); `render_qr()` encodes it.
- State + transition: `dev_state.h` `DEV_QR`; `app_state_set_state(DEV_QR)` wakes the display + aborts a settings flow; the receipt path `render_job.c` does `go(DEV_QR)` = `app_state_set_state(DEV_QR)` + `ui_render_state(DEV_QR)`.
- Auto-return: `idle_wait_or_qr_expiry()` + `ui_set_countdown(qrTimeoutSeconds)` return to `DEV_IDLE` on expiry. No new code.
- Decoupling pattern: `config-changed`/`firmware-update` invoke an app-registered callback so the cloud component doesn't depend on app/ui.

## Architecture

Firmware-only; reuses the QR/countdown/ack infra. The cloud component stays decoupled via a registered trigger callback, mirroring the existing `config-changed` callback.

### Components / changes

1. **`components/cloud/commands.c` — parse + dispatch trigger.**
   - Extract `action` (`cJSON_GetStringValue(... "action")`) and `payload.url` (`cJSON_GetObjectItem(cmd,"payload")` → `... "url"`).
   - Add a `type == "trigger"` branch:
     - if `action == "show_qr"` and `url` non-empty → invoke the registered trigger callback `s_trigger_cb(url)`, then `cloud_ack_command(id, true)`.
     - else → `cloud_ack_command(id, false)` (unknown action / missing url), no display change.
   - Add a registration setter `void commands_set_trigger_cb(void (*cb)(const char *url))` (mirrors the existing config-changed/firmware-update callback registration). If no cb is registered, ack `ok=false` (cannot display).

2. **Pure parse helper (for host-testability).** Extract the field extraction into a pure function, e.g. in `commands.c` (or a small `commands_parse.c`):
   `bool command_parse_trigger(const cJSON *cmd, const char **action_out, const char **url_out)` — returns whether the cmd is a well-formed `show_qr` trigger and yields `action`/`url`. The dispatcher uses it; the host harness tests it.

3. **App-side trigger callback** (`main/app_state.c` or wherever the other command callbacks are registered): a function that does `ui_set_qr_url(url); go(DEV_QR);` (same as the receipt flow), registered at startup via `commands_set_trigger_cb(...)`. This keeps `commands.c` free of ui/app_state dependencies.

### Data flow
```
poll task → cloud_get_commands → commands_handle_body
  cmd {type:"trigger", action:"show_qr", payload.url}:
     command_parse_trigger() → action, url
     s_trigger_cb(url):  ui_set_qr_url(url); go(DEV_QR)   // wakes display, starts countdown
     cloud_ack_command(id, true)   → cloud SETTLES the hold (charged)
  invalid (no cb / bad action / empty url):
     cloud_ack_command(id, false)  → cloud RELEASES the hold (refund); no screen change
countdown expiry → idle_wait_or_qr_expiry → DEV_IDLE
```

### Threading
`ui_set_qr_url` and `ui_render_state` take the LVGL port lock internally; the receipt flow already calls `go(DEV_QR)` from a task. Invoking the trigger callback from the poll-task context is therefore safe. (If a context issue surfaces on HIL, fall back to setting a pending-url flag that the poll/UI task consumes — but the direct call is expected to work, matching the receipt path.)

## Error handling

| Condition | Device action | Cloud effect (Spec A) |
|---|---|---|
| `type:"trigger"`, `action:"show_qr"`, valid url | show QR, `ack ok=true` | settle hold (charged) |
| missing/empty url, or `action != show_qr` | no display, `ack ok=false` | release hold (refund) |
| no trigger cb registered | `ack ok=false` | release hold (refund) |
| ack POST fails (network) | QR shown but unacked | hold stays `delivered`; cloud releases on TTL expiry (no charge); not re-delivered (already delivered) — fail-open toward the customer |

## Testing

- **Host (`tools/cfg-harness`):** add `test_trigger_command()` — feed a `commands` JSON body / a single command object through `command_parse_trigger()` and assert: valid `show_qr` yields the action + url; missing `payload`/`url` → not a valid trigger; `action:"other"` → not valid. (Pure cJSON, no LVGL/network.)
- **HIL (the real payoff — first full Spec A+B run):** with Spec A live on prod and a credit granted to the board's org, call `POST /api/v1/devices/{id}/trigger {action:"show_qr", payload:{url}}` (scoped key); via the passive UART reader confirm the device receives the command, shows the QR of the url, the countdown ticks and auto-returns to idle; then confirm in the cloud (DB/ `/api/v1/usage`) that the hold **settled** (per-device spend +1) — closing the loop. Also test a deliberately bad payload → device acks false → cloud releases (no charge).

## Build / flash (per BUILD.md)
`source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build`; `idf.py -p <port> flash`; read with `python3 tools/read-console.py 30` (NOT `idf.py monitor`).

## Out of scope
- A distinct trigger screen (reuse `DEV_QR`).
- Any action other than `show_qr`.
- Device-side fetching/validation of the url beyond non-empty (it only encodes the string into a QR — no SSRF surface).
