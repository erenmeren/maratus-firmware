# Pinned QR — Firmware Design Spec

*Date: 2026-07-22 · Counterpart: maratus-admin `docs/superpowers/specs/2026-07-21-pinned-qr-design.md` (cloud side SHIPPED to prod 2026-07-22).*

## Problem

Cloud now supports a per-device **pinned QR**: `GET /api/device/config` carries a
top-level `pin: { "url": string } | null` block (folded into the config ETag), and
a device command `type: "pin"` is enqueued (MQTT + HTTP poll) on every change.
Current firmware ignores both; a pinned device still shows the plain idle screen.

Target behavior:
- Pin set → whenever the device would show **idle**, it shows the pinned URL as a
  persistent QR (the QR screen layout, countdown hidden).
- Trigger while pinned → triggered QR shows normally (with countdown) for
  `qrTimeoutSeconds`, then returns to the **pin**, not idle.
- Pin cleared → device returns to the normal idle screen.
- Reboot/offline → pin survives (config JSON is NVS-cached) and shows right after
  the boot gate clears.

## Decisions (locked)

| Question | Decision |
|---|---|
| Source of truth | **Config only.** The `pin` command carries no state into firmware; its handler just requests a config refresh and acks. No dual state, no null-vs-url payload parsing. The ETag fold (cloud side) guarantees the refresh sees the change. Latency ≈ one poll iteration (~1 s) — acceptable. |
| State model | New lifecycle state **`DEV_PINNED`** in `dev_state_t`. `screen_for_state(DEV_PINNED) → SCREEN_QR`. A pure helper `dev_state_t home_state(bool pin_set)` returns `DEV_PINNED`/`DEV_IDLE` and replaces the hardcoded `DEV_IDLE` at every "go home" site. |
| Countdown | Suppressed **by render state**, not by config: `build_screen` skips `OBJ_COUNTDOWN` objects when the state being rendered is `DEV_PINNED`. The QR screen layout itself is unchanged (tenant layouts keep working). |
| QR content | Entering `DEV_PINNED` sets `s_qr_url` to the pinned URL (`ui_set_qr_url`); entering `DEV_QR` (trigger) overwrites it with the trigger URL as today; returning home re-sets it to the pin. |
| Sleep | `DEV_PINNED` sleeps **exactly like idle** (`should_sleep` accepts it). Tenants who want an always-on pinned QR disable screen sleep in Device Settings — no new knob. Wake-on-touch/trigger unchanged. |
| Paused | `DEV_PAUSED` is still a dead state in this build — no interaction to implement. |
| Old cloud / no pin | `pin` absent or null → `pin_set=false` → behavior byte-identical to today. |
| Version | Bump to **0.9.0** after build + HIL; BUILD.md hardware-validation entry. |

## Changes by component

### devcfg (pure, host-testable)
- `device_config_t` (+2 fields, sibling of `device{}`/`mqtt{}`):
  `bool pin_set; char pin_url[512];`
- `cfg_parse_json`: parse top-level `pin` object — `pin.url` non-empty string →
  `pin_set=true` + copy (truncate-safe); `pin` null/absent/malformed → `pin_set=false`.
  NVS persistence is free (raw JSON blob is what's cached).
- `dev_state.h`: add `DEV_PINNED`; `render_geom.c screen_for_state`: `DEV_PINNED → SCREEN_QR`.
- New pure helper (same file as `screen_for_state` or `sleep_policy.c` sibling):
  `dev_state_t home_state(bool pin_set)`.
- `sleep_policy.c should_sleep`: `state == DEV_IDLE || state == DEV_PINNED`.
- Host tests for all of the above (existing host-test harness).

### ui
- `ui.c`: remember the state passed to `ui_render_state` (static `s_render_state`);
  in `build_screen`'s object switch, `case OBJ_COUNTDOWN:` renders only when
  `s_render_state != DEV_PINNED`.
- No new widgets; `render_qr` already encodes `s_qr_url` on any screen containing
  an `OBJ_QR` object.

### cloud
- `commands.c dispatch_command`: new `"pin"` branch — invoke the existing
  config-changed callback (`app_state_request_config` via the registered cb),
  then `ack(id, true)`. Mirrors the `config-changed`/`refresh` branch verbatim.
- No `command_parse` changes (payload deliberately unused).

### main (app_state / app_main)
- New local helper `show_home(void)` in `app_state.c`: reads the live config's
  `pin_set`/`pin_url`; if pinned → `ui_set_qr_url(pin_url)`; sets
  `s_state = home_state(pin_set)`; `ui_render_state(s_state)`.
- Replace the three hardcoded go-home sites with `show_home()`:
  1. QR countdown expiry (`idle_wait_or_qr_expiry`),
  2. `boot_gate_fallback_offline`,
  3. poll_task first-successful-poll boot-gate clear.
- Config apply (`s_config_dirty` success path): after `ui_set_config`, if
  `s_state` is `DEV_IDLE` or `DEV_PINNED`, call `show_home()` instead of the
  blind `ui_render_state(s_state)` — this is what makes a pin set/clear/URL-change
  repaint within one poll cycle, and keeps `s_qr_url` in sync on URL changes.
- `app_state_set_state` wake special-case: unchanged (`DEV_QR`/`DEV_PROCESSING`
  still force a wake; entering `DEV_PINNED` does not wake a sleeping panel).

## Edge cases
- Trigger arrives while pinned: `DEV_QR` overwrites `s_qr_url`; on expiry
  `show_home()` restores the pin URL. Countdown renders normally in `DEV_QR`.
- Pin URL changes while pinned screen is showing: config-apply path calls
  `show_home()` → re-render with new URL (ETag changed, so a 200 is guaranteed).
- Pin cleared while a triggered QR is showing: expiry path calls `show_home()`
  which now returns `DEV_IDLE` — correct.
- Offline boot with cached pin: cached JSON parse sets `pin_set` before either
  boot-gate exit path runs → pin shows even with no network.
- Settings overlay / provisioning / setup flows: unchanged (they don't route
  through the go-home sites).

## Testing
- Host: cfg_parse pin block (set / null / absent / malformed / overlong URL);
  `home_state`; `should_sleep` with `DEV_PINNED`; `screen_for_state(DEV_PINNED)`.
- Build: clean `idf.py build`, no new warnings.
- HIL (b580): set pin from tenant UI → persistent QR (no countdown) within ~2 s;
  trigger → countdown QR → auto-return to pin; change URL → repaint; remove pin
  → idle; reboot → pin right after boot gate; screen-sleep still engages and
  wakes on touch.

## Out of scope
- Any paused-state interaction (dead state), per-screen "pinned" layout editing
  in the branding studio, always-on display override, NVS storage beyond the
  existing cached-config blob.
