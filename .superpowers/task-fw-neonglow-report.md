# NEON pre-rendered canvas glow — firmware (fw 0.16.0)

Branch `fix/neon-canvas-glow` off `main` (verified: `main` was up to date with
`origin/main`, clean working tree, before branching — no BLOCKED condition).

## Problem

LVGL's live NEON `shadow_width` is hard-capped at 64px (0.15.0 fix for a
boot-hang: uncapped it wedged the software shadow blur for seconds/frame,
starving `poll_task`) and is a single flat blur — undershooting both the
reach and the richness of the cloud studio preview's two-layer glow
(`blurPx = dim*(0.04+0.20s)`, neon `spreadPx = 2*blurPx`).

## Fix

Pre-render the glow into the QR canvas's own enlarged `ARGB8888` pixel
buffer ONCE at build time — zero per-frame cost, no LVGL shadow for NEON.
DROP/NONE are byte-for-byte unchanged (same `RGB565`, plate-sized canvas,
same `shadow_*` style path).

- `components/devcfg/{include/qr_style.h,qr_style.c}`: three new pure,
  host-testable functions — `qr_neon_halo_px` (spreadPx → canvas halo,
  capped to ≤plate and to `QR_NEON_CANVAS_SIDE_MAX_PX`=720 total side),
  `qr_glow_dist_px` (rounded-box SDF, Inigo Quilez `sdRoundBox`, distance
  outside the plate's own rounded rect), `qr_glow_alpha_at` (two-layer
  falloff: tight `a1=(1-d/blur)²` + wide `a2=0.55*(1-d/spread)²`,
  `alpha=255*max`).
- `components/ui/ui.c` `render_qr`: NEON path allocates
  `canvas_px + 2*halo` square `ARGB8888` canvas, fills transparent, new
  `paint_qr_neon_glow` helper writes glow pixels directly into the raw
  buffer (bypassing `lv_canvas_set_px`'s per-pixel invalidate — this is a
  ~270k-px loop), then draws the rounded `qrBg` plate + all modules/finders
  offset by `halo` on both axes (QR still lands exactly centered in the
  object box). NEON skips the lv_obj `radius`/`clip_corner` and all
  `shadow_*` styles (glow/rounding already baked into pixels). DROP/NONE
  path is untouched (`switch` collapsed to a single `if (DROP)` since NEON
  moved out of it, but produces identical output).
- Version 0.16.0: `main/Kconfig.projbuild` (committed-tracked) + local
  `sdkconfig` (gitignored, matches prior-task precedent).
- `BUILD.md`: new "NEON pre-rendered canvas glow (2026-07-24) — fw 0.16.0"
  section (approach + why, PSRAM numbers, host-test coverage), plus a short
  addendum on the existing 0.15.0 entry recording its hard-cap rationale
  (0.15.0 previously had no BUILD.md entry of its own).

## Gates

- `cd tools/cfg-harness && make clean && make test` → **ALL TESTS PASSED**
  (34/34: existing 33 + new `test_qr_neon_glow` — halo capping both ways +
  degenerate/negative inputs, SDF correctness at flat edges/square vs.
  rounded corners (Euclidean not Manhattan; rounding provably pulls the
  boundary inward near a corner), alpha falloff at d=0/d=blur/d=spread/
  monotonicity/degenerate blur+spread).
- `. ~/.espressif/v5.5/esp-idf/export.sh && idf.py build` (IDF v5.5.4) →
  **clean**, full 1870-target build; forced recompile of just
  `ui.c`/`qr_style.c` in isolation showed **zero warnings or errors** from
  either file (grepped the full log for `warning|error` — no hits).
  **NOT flashed** (out of scope per instructions).

## PSRAM numbers (plate=canvas_px≈260)

| strength (s) | blur | spread | halo | canvas side | buffer (ARGB8888) |
|---|---|---|---|---|---|
| 50 (s=0.5, spec example) | 36px | 73px | 73px | 406px | ~0.66MB |
| 100 (s=1.0, worst case)  | 62px | 125px | 125px | 510px | ~1.04MB |

Both well under PSRAM budget; halo never hits the plate-size cap at s=1.0
(125 < 260) nor the 720px absolute side cap (510 < 720). Freed via the
existing `on_qr_canvas_delete`/`LV_EVENT_DELETE` path, unchanged — it
already frees whatever pointer it's handed regardless of size/format.

## Concerns / follow-ups

- **Not HIL-verified** — build-verified only, per instructions ("Do NOT
  flash"). Needs a real-device pass: neon halo reach now visibly matches
  the studio preview at high strength (vs. 0.15.0's 64px-capped look), the
  glow reads as a bright core + soft wide bloom (not a single flat blur),
  DROP/NONE are visually unchanged from 0.15.0, corner-radius slider still
  visually tracks with NEON on, scan regression on 2 phones.
- `qr_glow_dist_px`/`qr_glow_alpha_at` use single-precision `float`
  (`sqrtf`, per-pixel) — fine for a boot-time-only ~270k-px pass on the
  P4's FPU, but flagging since the rest of `qr_style.c` is pure integer
  math; added `-lm` to the host harness `Makefile` for portability
  (macOS didn't strictly need it, Linux CI might).
- The two-layer falloff constants (0.55 outer-peak weight, the `a1`/`a2`
  split itself) are a "practical integer version" approximation of the
  cloud preview's actual stacked-gaussian CSS filter, per the task's own
  framing — not pixel-exact to the SVG output, so the HIL pass should
  specifically eyeball "reads as richer than 0.15.0's single blur," not
  expect a pixel-perfect match.
