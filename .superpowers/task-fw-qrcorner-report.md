# QR background corner + shadow — firmware (fw 0.12.0)

Spec addendum: `docs/superpowers/specs/2026-07-23-qr-style-options-firmware.md`
(no firmware-side addendum existed yet; cloud counterpart addendum is in
`docs/superpowers/specs/2026-07-23-qr-style-options.md` in ditto-admin).
Branch: `feat/qr-corner-shadow` off `main` (repo clean, up to date with
origin/main at `8063da5` before starting — verified via `git status` +
`git log main` vs `origin/main`).

## What shipped

1. **Pure geometry** (`components/devcfg/{include/qr_style.h,qr_style.c}`):
   `qr_corner_t` enum (`QR_CORNER_SQUARE|ROUNDED`) + `qr_bg_radius_px(canvas_px,
   corner)` — ~6% of `canvas_px` when rounded (matches the cloud's
   `lib/qr-svg.ts` `qrBackgroundRadius`), 0 when square, 0 for a degenerate
   `canvas_px <= 0`. Host-tested before any parse/render wiring (TDD).
2. **Parse** (`components/devcfg/{include/device_config.h,cfg_parse.c}`):
   `qr_corner`/`qr_shadow` fields on `device_config_t`, parsed from
   `config.qrCorner`/`qrShadow` — siblings of `qrShape`/`qrFg`/`qrBg` inside
   `"config"`. Each sanitizes independently and permissively: unknown/absent
   `qrCorner` → `"rounded"`; non-bool/absent `qrShadow` → `false`; no
   cross-field validation (unlike the fg/bg contrast guard).
3. **Render** (`components/ui/ui.c` `render_qr`): after finishing the canvas
   pixel buffer (unchanged — still a full-square RGB565 fill with `qr_bg`),
   sets `lv_obj_set_style_radius(canvas, qr_bg_radius_px(canvas_px,
   cfg->qr_corner), LV_PART_MAIN)` + `clip_corner` for parity with the
   existing `icon_circle` call site, and, when `qr_shadow`, a subtle
   `lv_obj_set_style_shadow_width/color/opa/offset_y` (24px / `#0F1428` /
   30% opa / 4px y-offset).
4. **Version**: `main/Kconfig.projbuild` default 0.11.0→0.12.0 (committed);
   `sdkconfig` on disk updated to match (gitignored, not tracked/committed —
   consistent with prior version bumps). `BUILD.md` gained a "QR background
   corner + shadow (2026-07-23) — fw 0.12.0" entry, "pending HIL".

## Alpha-approach decision (the interesting part)

The task flagged that the RGB565 canvas has no alpha channel, so "transparent
corners" needed a decision. Rejected ARGB8888 (doubles the QR canvas's PSRAM
footprint — worst case ~1.2MB vs ~600KB for a version-40 QR at a 720px box,
non-trivial after the earlier lodepng-PSRAM memory incident) and rejected a
second backing `lv_obj` plate (extra widget/alloc per render, plus it can't
actually punch transparent holes in an opaque RGB565 canvas sitting on top of
it — the square canvas would just cover the plate's rounded corners).

**Chosen: object-level `radius` clipping, no pixel-format change.**
`lv_canvas_class.base_class = &lv_image_class` (confirmed in
`managed_components/lvgl__lvgl/src/widgets/canvas/lv_canvas.c`), and
`lv_image.c`'s own `LV_EVENT_DRAW_MAIN` handler sets
`draw_dsc.clip_radius = lv_obj_get_style_radius(obj, LV_PART_MAIN)`
unconditionally — the same mechanism `lv_draw_sw_img.c` already uses to mask
image content to a rounded rect (verified: `clip_radius > 0` triggers
`lv_draw_sw_mask_radius_init`). This is exactly what `render_image` already
relies on for `icon_circle` (`lv_obj_set_style_radius(img, LV_RADIUS_CIRCLE,
...)`). So the QR canvas needed zero pixel-buffer changes — just a style call
after `lv_canvas_finish_layer`. Corners outside the radius aren't drawn at
all at composite time, so the screen behind shows through — visually
equivalent to "transparent corners" without ever allocating alpha pixels.

## Commits (on `feat/qr-corner-shadow`)

- `dddebf1` feat(devcfg): qr_bg_radius_px pure helper for QR corner-shadow (fw 0.12.0)
- `0a8e081` feat(devcfg): parse config.qrCorner/qrShadow into device_config_t
- `ed4627e` feat(ui): render_qr honors qrCorner/qrShadow + bump fw to 0.12.0

## Gates

- `cd tools/cfg-harness && make clean && make test` → **ALL TESTS PASSED**
  (32/32, incl. new `test_qr_corner_shadow` parse matrix — absent, both
  explicit values, explicit-`false`-kept, unknown corner, 3 non-bool-shadow
  shapes — plus new `qr_bg_radius_px` geometry assertions in `test_qr_style`
  and 2 new assertions in the full-fixture `test_parse` for the
  fields-absent default).
- `. ~/.espressif/v5.5/esp-idf/export.sh && idf.py build` → **clean**, full
  1870-target build. 12 pre-existing warnings, all in vendored code
  (FreeRTOS `atomic.h` unused-function, `esp_lvgl_port` deprecated
  `on_refresh_done`) — none in `devcfg`/`ui.c`.

## Concerns / follow-ups

- Not HIL-verified (out of scope here, same split as the 0.11.0 QR-style-
  options task). Needs a real-device pass: square vs rounded corner visible
  against a non-white/non-brand-bg screen background (to confirm the
  "transparent" clip actually reveals the right thing and isn't masked by
  something opaque behind the canvas), shadow on/off visually, and a scan
  regression on 2 phones (corner/shadow styling shouldn't affect scannability
  since it's purely outside the quiet zone's functional area, but worth
  confirming nothing shifted the canvas position).
- The shadow's fixed 24px/4px-offset px values don't scale with `canvas_px`
  the way `qr_bg_radius_px` does — a very small QR box (near the `side >= 32`
  floor) could get a shadow that looks disproportionately large relative to
  the plate. Not addressed here since the task spec gave fixed px values
  ("width ≈ 24px... slight y offset"); flagging for HIL eyeball check.
- LVGL has no SVG-style `feDropShadow`/`stdDeviation` blur — the on-device
  shadow is a solid-blur rectangle-shadow approximation of the cloud
  preview's `rgba(15,20,40,0.35)` drop shadow, not a pixel match. Documented
  in BUILD.md as an intentional approximation.
