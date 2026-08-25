# Pinned QR firmware — SDD ledger
Plan: docs/superpowers/plans/2026-07-22-pinned-qr-firmware.md
Branch: feat/pinned-qr (base 8e55966)

Task 1: complete (8e55966..596099b, pin config parse + 6 host cases, ALL PASS + idf.py clean, review SPEC ✅ + Approved, no findings).
Task 2: complete (596099b..4269a5c, DEV_PINNED + home_state + sleep parity, 25/25 host + idf clean, review SPEC ✅ + Approved).
Tasks 3+4: complete (4269a5c..a973bef, countdown suppression + pin command ack/refresh, idf clean ×2, review SPEC ✅ + Approved, no findings).
Task 5: complete (a973bef..f1c87bb, show_home + 3 site rewire + config-apply repaint, edge-case traced, review SPEC ✅ + Approved). IMPORTANT follow-up folded into Task 6: widen ui.c s_qr_url 256→512 (+ESP_LOGW on truncation) — pre-existing, pin makes it consequential.
Task 6 + fix: complete (fd47c7d s_qr_url 512B fix, 7d5debc 0.9.0+BUILD.md, d413ce8 final-fix wave [config-retry + stale-url clear + 3 host tests]).
=== FINAL review (opus): round1 With-fixes → fix wave d413ce8 → verified → READY TO MERGE = YES. Remaining gate: HIL on b580 (BUILD.md checklist + set-pin-while-asleep case). NOT merged; branch pushed.
