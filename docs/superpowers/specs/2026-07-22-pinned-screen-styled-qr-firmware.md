# Pinned Screen + Styled QR — Firmware Design Spec

*2026-07-22 · approved in conversation. Cloud counterpart: ditto-admin
`docs/superpowers/specs/2026-07-22-screen-terminology-and-pinned-screen.md`. fw 0.10.0.*

## 1. Editable pinned screen
- Cloud config now ships an 8th screen `"pinned"` (QR-screen-like layout, no countdown).
- Add `SCREEN_PINNED` to the screen enum; parse `config.screens.pinned` like the others.
- `screen_for_state(DEV_PINNED)` → `SCREEN_PINNED` **when that screen has objects**;
  fallback to `SCREEN_QR` when absent/empty (old cached configs) — pure helper, host-tested.
- Countdown suppression for `DEV_PINNED` renders stays (guards a user-placed countdown).

## 2. Styled QR (rounded modules)
- Replace `lv_qrcode` usage in `render_qr` with a custom draw: `qrcodegen` (already
  vendored) encodes; draw on `lv_canvas`: dark modules as filled circles
  (diameter ≈ 0.85×module), the three finder patterns as rounded-rects
  (outer ring + inner dot, radius ≈ 1/3 of their size). Quiet zone ≥ 4 modules.
  ECC level M (keep current). Scale to the config object's box as today; fallback
  URL behavior unchanged.
- Applies to every QR render (trigger, pinned, setup screens) — one code path.
- Host-testable pure part: module→pixel geometry helper (sizing, dot radius,
  finder detection) in a devcfg/render-side pure unit if practical.
- **HIL gate: scannability** — verify with at least 2 phones (iOS + Android camera)
  at arm's length on b580 before OTA publish; if flaky, increase dot diameter
  toward 1.0 (square-ish) and retest.

## 3. Wording
`components/ui/ui.c` setup screen: "Add a printer to a store" → "Add a screen to a store".

## Compatibility
Old cloud (no pinned screen in payload): fallback path keeps QR-screen behavior.
New cloud + old firmware: unknown screen key ignored by parser. No NVS/schema impact.

## Testing
Host: screen enum/parse/pinned-fallback helper + QR geometry helper. Build clean.
HIL: pinned shows edited pinned layout; trigger QR + pinned QR both scan (2 phones);
countdown absent on pinned; version 0.10.0 reported.
