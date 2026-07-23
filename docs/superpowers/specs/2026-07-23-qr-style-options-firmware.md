# QR Style Options — firmware (fw 0.11.0)

*2026-07-23. Cloud counterpart: ditto-admin docs/superpowers/specs/2026-07-23-qr-style-options.md.*

- Parse top-level `qrShape` ("classic"|"soft"|"rounded"|"dots"; unknown/absent → rounded)
  + `qrFg`/`qrBg` hex (malformed/absent → #111111 / #ffffff) into device_config_t
  (enum `qr_shape_t` + two lv_color-able u32s). Trust cloud's contrast guard but
  keep a defensive same-color check (fg==bg → defaults).
- render_qr: paint canvas bg with qrBg (incl. quiet zone), modules in qrFg;
  shape variants — classic: filled squares; soft: rounded-rect modules (r≈0.25×module);
  rounded: current circles (0.85×); dots: circles 0.7×. Finder patterns: classic → square
  rings; others → rounded rings (current draw). Extend qr_style.c pure helpers
  (per-shape dot geometry) + host tests (defaults, parse matrix, geometry per shape).
- Old cloud (fields absent) → identical to today. Version 0.11.0 + BUILD.md entry
  "pending HIL (scan all 4 shapes + a colored combo, 2 phones)".
- Gates: cfg-harness ALL PASS + idf.py build clean (IDF 5.5 only).
