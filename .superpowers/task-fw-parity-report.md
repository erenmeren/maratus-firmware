# QR parity formulas — task report

Branch: `feat/qr-parity-formulas` (off `main`, verified clean/up-to-date before creating)

## Goal
Device QR plate corner+shadow must visually match the cloud branding studio preview.
Root cause of "shadow barely visible / radius mismatched": the old firmware curves
(`qr_bg_radius_px` fixed 6%, `qr_shadow_width_px` fixed 8..48px, `qr_shadow_opa_pct`
fixed 20..90%/35..100%) were sized for one reference canvas and didn't scale with the
plate's actual rendered pixel size — a small device canvas got a proportionally tiny
shadow/radius next to the studio's larger preview canvas at identical settings.

## What changed
1. **`components/devcfg/cfg_parse.c` + `include/device_config.h`** — `config.qrCorner`
   enum ("square"|"rounded") replaced by `config.qrCornerRadius` (0..100 int, 0=square).
   `qr_corner_radius_of()` mirrors the cloud's `sanitizeQrCornerRadius`: valid number wins
   (clamped+rounded); else legacy `qrCorner` maps 1:1 (rounded→24, square→0); else default 24.
   `device_config_t.qr_corner` (`qr_corner_t`) → `qr_corner_radius` (`int`).
2. **`components/devcfg/qr_style.c` + `include/qr_style.h`** — canonical dim-relative
   formulas, ported exactly from `ditto-admin lib/qr-svg.ts`:
   - `qr_corner_radius_px(dim, v) = round(dim * 0.30 * v/100)` replaces `qr_bg_radius_px`.
   - `qr_shadow_params(mode, strength, dim)` (struct: `blur_px`, `offset_y_px`, `opa`
     [0..255], `spread_px`) replaces `qr_shadow_width_px`/`qr_shadow_opa_pct`. DROP:
     `blurPx = dim*(0.04+0.20*s)`, `offsetY = dim*0.02`, `opa = round((0.25+0.50*s)*255)`.
     NEON: `offsetY=0`, `opa=255`, `spreadPx = 2× the UNROUNDED blur` (not `2*blur_px`,
     to avoid double-rounding drift vs the cloud's float math).
   - Int math throughout, round-half-up (`(num+half)/scale` idiom), consistent with the
     file's existing style.
3. **`components/ui/ui.c` `render_qr`** — calls the new functions with `dim = canvas_px`.
   DROP: `shadow_width = blurPx*2`, `ofs_y = offsetYPx`, `opa` direct. NEON:
   `shadow_width = spreadPx*2`, `ofs 0`, `opa 255`, **plus `shadow_spread = blurPx/2`**
   (see calibration note below).
4. **Version bump** — `main/Kconfig.projbuild` `DITTO_FW_VERSION` 0.13.0→0.14.0 (local
   `sdkconfig`, gitignored/generated, updated to match on disk — not committed).
   `BUILD.md` changelog entry with root cause + formulas + pending-HIL note.

## Shadow calibration choices (LVGL vs CSS)
LVGL's `shadow_width` is a blur radius that extends on **both sides** of the shadow's
base rect, unlike CSS `box-shadow`'s one-sided blur-radius — so `shadow_width = blurPx*2`
calibrates it to read like the CSS blur it mirrors (DROP). NEON's width is `spreadPx*2`
(and `spreadPx` is itself `2×blurPx`), so NEON ends up ~4× DROP's tight-blur width — a
fuller halo, matching the doc comment's "brighter/fuller" intent. Per the task's fallback
guidance, I additionally set `shadow_spread = blurPx/2` on NEON only: at full opacity
(255) the blur-only glow still read weak in review because LVGL's shadow falls off from
a zero-area base rect; `shadow_spread` widens that base rect so the halo has visible
substance before the blur falloff, without touching DROP (which is calibrated against
CSS `box-shadow`'s single blur pass and didn't need it).

## Gates
- `tools/cfg-harness`: **31/31 tests pass** (`make test`), including new concrete-value
  matrices: `qr_corner_radius_px` (dim=260,v=24→19 spec example + clamps + degenerate),
  `qr_shadow_params` (dim=260,s=0.5 → blur=36, drop opa=128, neon spread=73 — all three
  spec examples match exactly) + dim-scaling + strength endpoints + degenerate/negative
  `dim`. `qrCornerRadius` parse matrix: value/clamp/absent/legacy-both-directions/
  modern-wins-over-legacy.
- `idf.py build` (IDF v5.5.4 via `~/.espressif/v5.5/esp-idf/export.sh`): **clean, exit 0**.
  12 warnings total, all pre-existing vendor/SDK noise (FreeRTOS `atomic.h` unused
  inline functions, one `esp_lvgl_port` deprecated-callback warning) — **zero new
  warnings** from any touched file. Binary confirms `0.14.0` embedded.

## Commits (4, on `feat/qr-parity-formulas`)
- `2d8e4f5` feat(devcfg): parse config.qrCornerRadius replacing qrCorner enum
- `ba10e5a` feat(qr_style): canonical dim-relative corner+shadow formulas (cloud parity)
- `8623d2b` feat(ui): render_qr wired to canonical corner+shadow formulas
- `fc0c253` chore: bump firmware version to 0.14.0 (QR corner+shadow parity)

## Not done (per BUILD.md convention, out of scope for this task)
- HIL verification (visual parity vs studio preview, scan regression, 2 phones) —
  flagged "pending HIL" in the BUILD.md entry, same as 0.12.0/0.13.0 were before their
  HIL passes.
