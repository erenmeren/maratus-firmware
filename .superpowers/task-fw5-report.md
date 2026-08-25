# Task 5 report — main: show_home() + call-site rewiring

Branch: `feat/pinned-qr` (verified via `git branch --show-current` before starting).
Commit: `f1c87bb` — "feat(pin): go-home routes to pinned QR; config apply repaints pin changes"
File changed: `main/app_state.c` (+19/-7), explicit `git add main/app_state.c`.

## What changed

1. Added `#include "render_geom.h"` (for `home_state()`) after the existing
   `#include "device_config.h"`.
2. Added `static void show_home(void)` just above `idle_wait_or_qr_expiry`
   (before all three call sites, so no forward declaration needed):
   ```c
   static void show_home(void) {
       const device_config_t *cfg = s_cfg_buf[s_cfg_live];
       bool pinned = cfg && cfg->valid && cfg->pin_set;
       if (pinned) ui_set_qr_url(cfg->pin_url);
       s_state = home_state(pinned);
       ui_render_state(s_state);
   }
   ```
   Buffer access adapted to the file's actual declarations: `s_cfg_buf` is
   `device_config_t *s_cfg_buf[2]` (array of pointers) and `s_cfg_live` is the
   live index — this is the exact pattern already used at call sites like
   `run_settings_flow` (`const device_config_t *cfg = s_cfg_buf[s_cfg_live];`)
   and the validity flag is `cfg->valid` (matches usage at lines 511/559/564).
   No adaptation beyond using that existing idiom was needed.
3. Replaced the three hardcoded `s_state = DEV_IDLE; ui_render_state(DEV_IDLE);`
   pairs with `show_home();`:
   - `idle_wait_or_qr_expiry` (QR-countdown expiry)
   - `boot_gate_fallback_offline`
   - `poll_task` boot-gate-clear (first successful poll)
4. Config-apply success path: after `s_cfg_live = next;` and the
   `mqtt_start`/`mqtt_stop` branch, replaced the blind
   `ui_render_state(s_state);` with:
   ```c
   if (s_state == DEV_IDLE || s_state == DEV_PINNED) show_home();
   else ui_render_state(s_state);
   ```
   with a comment explaining this is what repaints pin set/clear/URL-change
   within one poll cycle.

## Ordering check (config-apply path)

`s_cfg_live = next;` executes *before* the new `if (s_state == DEV_IDLE ||
DEV_PINNED) show_home(); else ...` line — confirmed by reading the surrounding
code before editing (the assignment was already several lines above the old
`ui_render_state(s_state)` call, and the edit only touched the last line of
that block). So `show_home()` reads `s_cfg_buf[s_cfg_live]` == the freshly
published buffer (`next`), not the stale one. No reordering was necessary.

## `app_state_set_state` wake check

`show_home()` writes `s_state` directly and calls `ui_render_state`, never
`app_state_set_state` — confirmed by inspection (grep shows `app_state_set_state`
is untouched by this task and its only special-cases remain `DEV_PROCESSING`/
`DEV_QR`). Entering `DEV_PINNED` via `show_home()` therefore does not force a
display wake, per requirement.

## Site NOT touched (intentionally, out of plan scope)

`app_state_run`'s calloc-failure fallback (`ui_render_state(DEV_IDLE)` when
`s_cfg_buf[0]`/`s_cfg_buf[1]` allocation fails) was left as a bare
`ui_render_state(DEV_IDLE)` — it is not one of the plan's three named sites,
and calling `show_home()` there would be unsafe/meaningless since the config
buffers themselves failed to allocate.

## Edge-case trace (spec "Edge cases" section)

- **Trigger arrives while pinned → expiry restores pin URL.** A trigger sets
  `s_state = DEV_QR` via `app_state_set_state`, which also calls
  `ui_set_qr_url` (trigger handler, unchanged) to the trigger's URL — this
  overwrites `s_qr_url`. On countdown expiry, `idle_wait_or_qr_expiry` now
  calls `show_home()`, which re-reads the live config and — since `pin_set`
  is still true — calls `ui_set_qr_url(cfg->pin_url)` again before rendering
  `DEV_PINNED`. Confirmed restored.
- **Pin cleared while a triggered QR is showing → idle.** Same expiry path:
  `show_home()` reads `cfg->pin_set == false` now (cleared via a prior
  config-apply), so `pinned=false`, `s_state = home_state(false) == DEV_IDLE`,
  and `ui_set_qr_url` is skipped. Confirmed idle, not showing a stale pin QR.
- **Pin URL changes while pinned screen is showing → repaint.** Config-apply
  success path: `s_state == DEV_PINNED` at the time of apply → the new
  `if (... || s_state == DEV_PINNED) show_home();` branch fires, and
  `show_home()` re-reads the just-published `cfg->pin_url` and calls
  `ui_set_qr_url` with the new value, then re-renders. Confirmed.
- **Offline boot with cached pin.** `cloud_config_load_cached` (in
  `app_state_run`, unchanged/out of this task) parses the cached JSON into
  `s_cfg_buf[s_cfg_live]` before `poll_task` starts, so `pin_set`/`pin_url`
  are populated pre-boot-gate. Both boot-gate exit paths
  (`boot_gate_fallback_offline` and poll_task's boot-gate-clear) now call
  `show_home()`, which reads that already-cached buffer — pin shows even with
  no network, before any live fetch. Confirmed by code inspection (no runtime
  HIL performed, per task scope — HIL is controller/user-driven per plan).

## Gates

- `cd tools/cfg-harness && make && ./run` — clean rebuild, all 25 test groups
  printed OK, final line `ALL TESTS PASSED`.
- `idf.py build` (IDF 5.5 env sourced from `~/.espressif/v5.5/esp-idf/export.sh`)
  — clean full build, then a touch+rebuild of `app_state.c` alone grepped for
  `warning|error`: no matches. `Project build complete.`

## Concerns / notes for reviewer

- None outside of scope items noted above. The `s_qr_url` buffer in `ui.c` is
  256 bytes while `pin_url` is 512; `ui_set_qr_url` already truncate-safely
  copies (`strncpy` + explicit NUL), so an overlong pin URL degrades to a
  truncated QR target rather than overflowing — this is pre-existing
  `ui_set_qr_url` behavior, not something this task changed, but worth a
  reviewer's eye if 512-byte pin URLs are expected to round-trip losslessly
  onto the QR screen.
