# QR Style Options — firmware (fw 0.11.0)

Spec: `docs/superpowers/specs/2026-07-23-qr-style-options-firmware.md`.
Branch: `feat/qr-style-options` off `main` (repo was clean, 1 commit ahead of
origin — the spec-only docs commit `4bec19c` — before starting; expected).

## What shipped

1. **Parse** (`components/devcfg/{include/device_config.h,include/qr_style.h,cfg_parse.c}`):
   `qr_shape_t` enum (`QR_SHAPE_CLASSIC|SOFT|ROUNDED|DOTS`) + `qr_fg`/`qr_bg`
   `uint32_t` (0xRRGGBB) fields on `device_config_t`. Parsed from
   `config.qrShape`/`qrFg`/`qrBg` — confirmed siblings of `clockTimezone`/
   `qrTimeoutSeconds` inside the `"config"` object, not document-root (matches
   the existing `qrTimeoutSeconds` parse site exactly). Defaults: absent/
   unknown shape → `rounded`; malformed/absent hex → `#111111`/`#ffffff`;
   defensive `fg == bg` → both reset to defaults (belt-and-suspenders on top
   of the cloud's own contrast guard).
2. **Geometry** (`components/devcfg/qr_style.c`): `qr_dots_diameter_px`
   (~0.7×), `qr_soft_radius_px` (~0.25×, clamped ≤ module_px/2),
   `qr_finder_radius_px_for(shape, size)` (0 for classic, existing radius
   otherwise), and `qr_module_style(shape, module_px)` — one pure function
   returning `{kind, size, offset, radius}` per shape that both host tests
   and `ui.c` consume, so shape-selection logic exists exactly once.
3. **Render** (`components/ui/ui.c` `render_qr`/`draw_qr_finder`): canvas bg
   (incl. quiet zone) now `lv_color_hex(qr_bg)` instead of hardcoded white;
   modules/finders `lv_color_hex(qr_fg)` instead of black; module draw
   dispatches on `qr_module_style()` (square/rounded-square/circle); finder
   rings pick square vs rounded corners via `qr_finder_radius_px_for`. Same
   PSRAM canvas lifecycle, unchanged. Old cloud (fields absent) renders
   pixel-identical to 0.10.0.
4. **Version**: `main/Kconfig.projbuild` default 0.10.0→0.11.0 (committed);
   `sdkconfig` on disk updated to match (gitignored — dev creds live there,
   never tracked, consistent with prior version bumps). `BUILD.md` gained a
   "QR style options (2026-07-23) — fw 0.11.0" entry, "pending HIL (scan all
   4 shapes + a colored combo, 2 phones)".

## Commits (on `feat/qr-style-options`)

- `e0541ea` feat(devcfg): parse config.qrShape/qrFg/qrBg + per-shape QR geometry
- `9db47f9` feat(ui): render_qr honors qrShape/qrFg/qrBg
- `22f0b28` build: bump fw to 0.11.0 + BUILD.md QR-style-options entry

## Gates

- `cd tools/cfg-harness && make && ./run` → **ALL TESTS PASSED** (30/30,
  incl. new `test_qr_shape_colors` parse matrix — all 4 shapes, unknown
  shape, malformed hex each side, fg==bg — and extended `test_qr_style`
  geometry assertions for dots/soft/finder-per-shape/`qr_module_style`).
- `. ~/.espressif/v5.5/esp-idf/export.sh && idf.py build` → **clean**, full
  rebuild (sdkconfig touch triggered reconfigure of all 1868 targets). Only
  warnings present are pre-existing ones from vendored FreeRTOS
  (`atomic.h` unused-function) and `esp_lvgl_port` (`on_refresh_done`
  deprecated) — nothing new from `devcfg`/`ui`.

## Concerns / follow-ups

- Not HIL-verified yet (out of scope for this task per the spec — BUILD.md
  entry says so explicitly). Needs a real scan pass: all 4 shapes at default
  colors, plus one colored (non-default fg/bg) combo, on 2 phones (iOS +
  Android camera), before calling 0.11.0 done.
- `qr_soft_radius_px`'s clamp (`module_px/2`) means at very small module
  sizes (≤3px, i.e. tiny QR boxes) the "soft" shape's radius saturates to
  1px and looks visually close to "classic" — acceptable given the existing
  `side >= 32` clamp upstream keeps module_px reasonably large in practice,
  but worth an eyeball check during HIL if a tenant sets an unusually small
  QR object box.
