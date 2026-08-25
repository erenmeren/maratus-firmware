# Pinned screen + styled QR — firmware implementation report

Branch: `feat/pinned-screen-styled-qr` (off `main` @ `7378180`).
Spec: `docs/superpowers/specs/2026-07-22-pinned-screen-styled-qr-firmware.md`.

Commits:
- `e7a6779` feat(devcfg,ui): add SCREEN_PINNED, screen_for_pinned cfg-aware helper
- `575877a` feat(ui): styled QR (rounded modules + finder rings) via qrcodegen + lv_canvas
- `2bfb6f9` build: bump fw to 0.10.0 + BUILD.md pinned-screen/styled-QR entry

## 1. SCREEN_PINNED

**What**: 8th screen enum value `SCREEN_PINNED`, appended *last* (index 7) in
`components/devcfg/include/device_config.h` — matches cloud's
`lib/printer-layout.ts`, which also appends `"pinned"` last to
`PRINTER_SCREENS` specifically so index-based assumptions elsewhere aren't
disturbed. `components/devcfg/cfg_parse.c`'s `SCREEN_NAMES[SCREEN_COUNT]`
got `"pinned"` appended to match; the existing generic `parse_screen` loop
(`for (int i = 0; i < SCREEN_COUNT; i++) ...`) picks it up with **no other
parser changes** — it was already screen-count-driven, not hardcoded per
screen.

**Cfg-aware wrapper**: `screen_for_state(dev_state_t)` in
`components/devcfg/render_geom.c` is left untouched — it's a pure,
cfg-unaware function (existing host tests assert
`screen_for_state(DEV_PINNED) == SCREEN_QR`, kept as documented fallback)
used nowhere else. New `dev_screen_t screen_for_pinned(const device_config_t
*cfg)` (same file) is the cfg-aware version: `SCREEN_PINNED` when
`cfg->screens[SCREEN_PINNED].n > 0`, else `SCREEN_QR` (NULL/invalid cfg also
falls back to `SCREEN_QR`).

**Wiring** (`components/ui/ui.c` `ui_render_state`): `ui.c` already carries a
file-scope `static const device_config_t *s_cfg` (set by `ui_set_config`,
read elsewhere without locking — e.g. `ui_current_cfg()`, `render_wordmark`),
so no new plumbing was needed — `ui_render_state` was changed from
`screen_for_state(st)` to `(st == DEV_PINNED) ? screen_for_pinned(s_cfg) :
screen_for_state(st)`. Comment in-line explains the split.

**Countdown suppression**: unchanged — `build_screen`'s
`case OBJ_COUNTDOWN: if (s_render_state != DEV_PINNED) ...` is keyed on the
*lifecycle state* (`s_render_state`, set from `st` regardless of which
screen was picked), not the screen, so it correctly suppresses on both the
`SCREEN_PINNED` and `SCREEN_QR`-fallback paths.

**TDD evidence** (`tools/cfg-harness/test_cfg.c`):
- `test_parse`: added assertion that the existing full fixture (predates the
  `"pinned"` key) parses to `cfg.screens[SCREEN_PINNED].n == 0` — no crash on
  old cached configs.
- `test_screen_for_pinned` (new): NULL cfg, invalid cfg, valid-but-empty
  pinned screen → `SCREEN_QR`; valid + `n>=1` → `SCREEN_PINNED`.
- `test_pinned_screen_parse` (new): inline JSON with a 2-object pinned screen
  parses both objects correctly (`OBJ_QR`, `OBJ_TEXT` with text/font/align);
  empty-array pinned screen and pinned-key-absent both fall back correctly.
- `test_map`'s existing `screen_for_state(DEV_PINNED) == SCREEN_QR` assertion
  kept as-is (comment reworded to "cfg-unaware fallback" for clarity).

## 2. Styled QR (rounded modules + finder rings)

**What/where**: `components/ui/ui.c` `render_qr` rewritten. Was: create an
`lv_qrcode` widget, `lv_qrcode_update`. Now: `qrcodegen_encodeText` (ECC
Medium — unchanged from before, `qrcodegen_VERSION_MIN..MAX`,
`qrcodegen_Mask_AUTO`, `boostEcl=true`) encodes the URL into a temp
`qrcodegen` buffer; an `lv_canvas` is created and drawn onto via
`lv_canvas_init_layer` + `lv_draw_rect` (RGB565 buffer):
- Dark modules → filled circles (`lv_draw_rect` with `radius =
  LV_RADIUS_CIRCLE`), diameter `qr_dot_diameter_px(module_px)` ≈ 0.85×
  module, centered in the module cell.
- The three finder patterns (top-left, top-right, bottom-left 7×7 blocks) are
  skipped in the per-module loop (`qr_in_finder_block`) and instead drawn
  once each as three nested rounded rects: outer 7-module dark ring, middle
  5-module light hole, inner 3-module dark dot — corner radius
  `qr_finder_radius_px(shape_px)` ≈ shape_px/3 for each, per spec.
- Quiet zone: 4 modules (`QR_QUIET_MODULES`) of untouched white canvas margin
  on all sides, inside the object's box.
- Same fallback URL behavior as before (`s_qr_url[0] ? s_qr_url :
  "https://ditto.app"`), same box-fit scaling (module size derived from
  `min(box_w, box_h)`, canvas centered in the full box).
- One code path: every `OBJ_QR` render (trigger, pinned, setup) goes through
  this same function — no per-screen QR variants.

**Pure geometry** (host-testable): new `components/devcfg/qr_style.{h,c}` —
no LVGL/qrcodegen dependency:
- `qr_module_px(box_side, modules, quiet)` — module pixel size, floors,
  never < 1 for a positive module count.
- `qr_canvas_px(module_px, modules, quiet)` — total canvas side.
- `qr_dot_diameter_px(module_px)` — ~0.85× module, clamped to `[1,
  module_px]`.
- `qr_finder_radius_px(shape_px)` — ~shape_px/3, clamped ≥ 1.
- `qr_in_finder_block(modules, mx, my)` — true inside any of the three
  7×7 finder blocks at the fixed corners.

**TDD evidence**: wrote `qr_style.h` + failing tests first, added
`qr_style.c` to `tools/cfg-harness/Makefile`'s `SRCS` *before* creating the
`.c` file — confirmed a genuine red state (`make: *** No rule to make target
'.../qr_style.c'`) — then implemented `qr_style.c` and reran to green.
`test_qr_style` in `test_cfg.c` covers: module_px (exact division, floor,
box-smaller-than-modules clamp to 1, degenerate 0-modules → 0), canvas_px,
dot diameter (round-half-up + both clamps incl. the `module_px < 1` edge),
finder radius (both finder shape sizes + the ≥1 clamp), and finder-cell
membership for all three corners of a 21-module (version-1) QR plus
non-finder interior/corner points and a too-small module count.

**Canvas memory approach**: the canvas pixel buffer
(`canvas_px × canvas_px × 2` bytes, RGB565) and both `qrcodegen` scratch
buffers (`qrcodegen_BUFFER_LEN_MAX` ≈ 3.9KB each) are `heap_caps_malloc(...,
MALLOC_CAP_SPIRAM)` — never stack (this runs on the LVGL render task) and
never internal DRAM (reserved for Wi-Fi/SDIO/TLS per the earlier M5a/branding
PSRAM fixes noted in project memory). The two scratch buffers are freed
immediately after encode; the canvas pixel buffer is registered on
`LV_EVENT_DELETE` (`on_qr_canvas_delete`) and freed when the QR canvas object
is deleted — which happens every screen rebuild via `ui_render_state`'s
`lv_obj_delete(old)` on the previous screen tree. This mirrors the existing
`render_image`/`img_src_t` pattern exactly (decode-then-free-on-redraw,
nothing lingers in PSRAM between builds).

**Not done / deferred**: HIL scannability check (spec's explicit gate — "at
least 2 phones, iOS + Android, arm's length, before OTA publish"). This
requires physical hardware (b580) and is out of scope for this code-only
pass; flagged in BUILD.md and below.

## 3. Wording

`components/ui/ui.c` `SETUP_STEPS[1]`: `"2.  Add a printer to a store"` →
`"2.  Add a screen to a store"`. One-line change, no test needed (static
string).

## 4. Version bump

- `sdkconfig` (gitignored, dev-local — confirmed via `git check-ignore` and
  precedent from the 0.9.0 bump commit `7d5debc`, which touched only
  `Kconfig.projbuild` + `BUILD.md`): `CONFIG_DITTO_FW_VERSION="0.10.0"`.
- `main/Kconfig.projbuild` default: `"0.10.0"`.
- `BUILD.md`: new `## Pinned screen + styled QR (2026-07-22, pending HIL) —
  fw 0.10.0` section, HIL checklist line reads *"Pinned screen + styled QR
  (0.10.0) — pending HIL (scannability: 2 phones)."* per the task's exact
  wording, plus the full checklist (pinned-vs-fallback screen behavior,
  2-phone scan test with the "widen dot diameter" fallback instruction,
  wording check, version-on-heartbeat check).

## 5. Gates (final run, clean working tree except untracked `.superpowers/`)

**Host suite** (`cd tools/cfg-harness && make && ./run`) — all 28 tests pass:
```
test_parse OK
test_geom OK
test_qr_style OK
test_zsort OK
test_map OK
test_screen_for_pinned OK
test_pinned_screen_parse OK
test_home_state OK
... (20 more pre-existing tests, all OK)
ALL TESTS PASSED
```

**`idf.py build`** (IDF 5.5, `. ~/.espressif/v5.5/esp-idf/export.sh`) — clean,
`grep -iE "warning|error"` on a full rebuild of the touched components (ui,
devcfg) returned nothing:
```
[9/10] ... check_sizes.py ...
ditto-firmware.bin binary size 0x196160 bytes. Smallest app partition is
0x200000 bytes. 0x69ea0 bytes (21%) free.
Project build complete.
```

## Concerns / open items for HIL

- **Scannability is unverified** — the finder-ring "hole" (light 5-module
  square) and dot sizing (0.85×) are per-spec defaults, not yet phone-tested.
  Spec's own fallback: if flaky, widen dot diameter toward 1.0× (more
  square, less "rounded") and retest — `qr_dot_diameter_px`'s `0.85f`
  constant in `components/devcfg/qr_style.c` is the single knob to turn.
- Pinned-screen editing UX on the cloud side (branding studio) is out of
  scope here — this task only covers the firmware parse/render half; the
  spec's cloud counterpart is `ditto-admin`'s
  `2026-07-21-pinned-qr-design.md` / `2026-07-22-screen-terminology-and-
  pinned-screen.md`.
