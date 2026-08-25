# QR shadow mode/strength/color — firmware (fw 0.13.0)

Device side of QR shadow modes (none/drop/neon). Cloud counterpart pending
(no cloud spec doc referenced this task — device-side only, per instructions).
Branch: `feat/qr-shadow-neon` off `main` (repo clean, up to date with
origin/main at `8152e18` before starting — verified via `git status` +
`git fetch origin` + `git rev-parse HEAD origin/main`).

## What shipped

1. **Parse** (`components/devcfg/{include/device_config.h,cfg_parse.c}`):
   replaced the old boolean `qr_shadow` field with `qr_shadow_mode`
   (`qr_shadow_mode_t`: `NONE|DROP|NEON`) + `qr_shadow_strength` (0..100
   int, clamped) + `qr_shadow_color` (0xRRGGBB), parsed from
   `config.qrShadowMode`/`qrShadowStrength`/`qrShadowColor` — siblings of
   `qrCorner`. `qrShadowMode` absent/unknown → `NONE`. Legacy migration:
   older cloud sent only a boolean `qrShadow` — when `qrShadowMode` is
   truly absent from the payload, `qrShadow: true` → `DROP`; a
   present-but-unknown `qrShadowMode` string is treated as "unknown", not
   "absent", so it stays `NONE` with **no** legacy fallback even if a
   legacy `qrShadow` is present alongside it. A present `qrShadowMode`
   always wins over a legacy `qrShadow`.
2. **Pure helpers** (`components/devcfg/{include/qr_style.h,qr_style.c}`):
   `qr_shadow_width_px(strength)` — linear `8..48`px, mode-independent —
   and `qr_shadow_opa_pct(strength, mode)` — linear `20..90` for DROP,
   `35..100` for NEON (NEON strictly brighter than DROP at every
   strength). Both re-clamp `strength` defensively.
3. **Render** (`components/ui/ui.c` `render_qr`): dispatches on
   `qr_shadow_mode`. DROP sets `shadow_width`/`shadow_color`/`shadow_opa`
   from the two curves plus `shadow_offset_y = width/6` ("resting on the
   surface"). NEON sets `offset_y = 0` (glow surrounds evenly), width
   `×1.5` of the DROP curve, and the brighter NEON opacity curve. NONE
   leaves the canvas's default no-shadow styles untouched. Shadow color now
   comes from `cfg->qr_shadow_color` (was a fixed `#0F1428` in 0.12.0).
4. **Version**: `main/Kconfig.projbuild` default `0.12.0`→`0.13.0`
   (committed); `sdkconfig` on disk updated to match (gitignored, not
   tracked/committed — consistent with prior version bumps). `BUILD.md`
   gained a "QR shadow mode/strength/color (2026-07-24) — fw 0.13.0" entry,
   "pending HIL".

## Sequencing note (intentional intermediate non-buildability)

Commit 1 removes the old `qr_shadow` bool field from `device_config_t` in
the same commit that adds the new fields (a true rename, not additive) —
`components/ui/ui.c` still read the old field at that point and would not
compile against the full IDF target until commit 3 rewired it. This is
fine: the task's TDD gate is the host harness (`tools/cfg-harness`, which
never includes `ui.c`), and it passes at every commit; the full
`idf.py build` gate was only required — and verified — once, at the final
state.

## Commits (on `feat/qr-shadow-neon`)

- `47173ae` feat(devcfg): parse config.qrShadowMode/qrShadowStrength/qrShadowColor
- `4807819` feat(devcfg): qr_shadow_width_px/opa_pct pure helpers for shadow strength
- `557cd61` feat(ui): render_qr honors qrShadowMode/Strength/Color + bump fw to 0.13.0

## Gates

- `cd tools/cfg-harness && make clean && make test` → **ALL TESTS PASSED**
  (33/33: existing 31 + new `test_qr_shadow_fields` (15-point matrix —
  absent, each explicit mode, unknown mode, legacy migration true/false/
  non-bool, modern-wins-over-legacy, strength pass-through/absent/clamp-low/
  clamp-high/non-number, color valid-with/without-#/malformed/absent) + new
  shadow-curve assertions appended to `test_qr_style` (width/opa endpoints,
  midpoint rounding, NEON>DROP invariant across the range, out-of-range
  clamping) + 4 updated assertions in the full-fixture `test_parse` for the
  new fields' fields-absent defaults).
- `. ~/.espressif/v5.5/esp-idf/export.sh && idf.py build` → **clean**, full
  1870-target build (run at final commit state). 12 pre-existing warnings,
  all in vendored code (FreeRTOS `atomic.h` unused-function,
  `esp_lvgl_port` deprecated `on_refresh_done`) — none in
  `devcfg`/`ui.c`, confirmed via grep against the build log.

## Concerns / follow-ups

- Not HIL-verified (out of scope here, same split as prior QR-style tasks
  0.11.0/0.12.0). Needs a real-device pass: DROP vs NEON vs NONE visibly
  distinct against a non-white/non-brand-bg screen background, strength
  slider low/mid/high showing a real width+opacity change, a custom
  `qrShadowColor` actually tinting the shadow, and a scan regression on 2
  phones.
- No cloud-side counterpart exists yet for `qrShadowMode`/
  `qrShadowStrength`/`qrShadowColor` (this task was firmware-only per
  instructions) — the device will render `NONE` (today's fw 0.12.0
  no-shadow-by-default look, since old cloud sends neither the new fields
  nor a truthy legacy `qrShadow`) until the cloud side ships.
- The DROP `offset_y = width/6` and NEON `×1.5` width multiplier are
  integer/float approximations chosen to match the task's descriptive spec
  ("y-offset ≈ width/6", "wider width (×1.5)") — not pixel-exact to any
  design mock, since none was given; flagging for HIL eyeball check same as
  the 0.12.0 shadow's fixed-px approximation was.
