# Rename "Receipt" → "Document" (Firmware, Spec 2) — Design

**Date:** 2026-06-27
**Repo:** `maratus-firmware`
**Status:** Approved design, pre-implementation
**Follows:** the cloud rename ([maratus-admin Spec 1], shipped to prod 2026-06-27). This is the firmware half — cosmetic; the device already works against the cloud's renamed `/d/{token}` URL because it just prints the URL the cloud returns.

## Problem

The cloud renamed the printed-artifact concept from "receipt" to "document" everywhere. The firmware still says "receipt" in identifiers, comments, UART logs, one on-screen string, and an embedded test asset. Bring it in line.

## Decisions (locked via brainstorming)

1. **No protocol/behavior change.** `/api/ingest` endpoint is unchanged; the device prints whatever URL the cloud returns. This is a pure rename.
2. **Total rename** of "receipt" → "document" in firmware source (identifiers, comments, logs, the one UI string, and the embedded asset).
3. **Build-only verification** (host tests + `idf.py build` + grep gate). **No HIL** — the embedded PNG bytes are unchanged so rendering is unaffected, and the build verifies the embed/symbol link.

## Scope

~32 occurrences across ~14 files (excluding `managed_components/` and `build/`).

### A) Internal identifiers + comments + UART log strings

- `cloud_post_receipt()` → `cloud_post_document()`: declaration in `components/cloud/include/cloud.h`, definition in `components/cloud/cloud.c`, and the two callers in `main/render_job.c` and `main/app_state.c`.
- "receipt"→"document" in comments and `ESP_LOGI`/log strings ("receipt ready: %s" → "document ready: %s", "receipt job" comments, etc.) across: `main/render_job.c`, `main/app_state.c`, `main/app_main.c`, `main/state.h`, `components/ui/include/ui.h`, `components/devcfg/include/device_config.h`, `components/render/render.c`, `components/render/png_encode.c`, `components/render/include/png_encode.h`.

### B) On-screen string

- `components/ui/ui_settings.c`: the Settings "Test print" item subtitle `"Send a sample receipt"` → `"Send a sample document"`.

### C) Multipart filename (cosmetic)

- `components/cloud/cloud.c`: the multipart body `filename="receipt.png"` → `filename="document.png"`. (The server reads the `file` form field, not the filename — purely cosmetic.)

### D) Embedded test asset

- `git mv components/assets/test_receipt.png components/assets/test_document.png`.
- `components/assets/CMakeLists.txt`: `EMBED_FILES "test_receipt.png" …` → `"test_document.png" …`.
- `components/assets/assets.c`: the IDF-generated symbol names derive from the filename — update the two `asm("_binary_test_receipt_png_start")` / `…_end` → `_binary_test_document_png_start`/`_end`. The local C aliases `test_png_start`/`test_png_end` (no "receipt" in them) stay, plus the comment "Symbols produced by EMBED_FILES "test_receipt.png"" → "…test_document.png".
- `tools/png-test/test_png.c`: update any `test_receipt.png` / symbol reference.

## Out of scope / unchanged

- The `/api/ingest` endpoint, the cloud command protocol, the QR/show_qr trigger feature, and all device behavior.
- `escpos_parser.c` (no "receipt" — an earlier grep false-positive on the letter "R").
- `managed_components/`, `build/` (vendored/generated).

## Testing

- **Host:** `cd tools/cfg-harness && make test` → all pass (the pure devcfg/cmd tests are unaffected; this guards against accidental damage).
- **Build (key gate):** `source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build` → clean. This verifies the renamed embed file + the `_binary_test_document_png_*` symbols resolve (a wrong symbol name fails the link).
- **Grep gate:** `grep -rinE "receipt" components main tools --include="*.c" --include="*.h"` (excluding `managed_components`/`build`) → **0**.
- No HIL (per decision).

## Risks

- The one non-mechanical bit is the embedded asset: the `asm("_binary_test_document_png_*")` symbol names MUST exactly match the new filename (`test_document.png` → `_binary_test_document_png_*`), or the build fails at link — caught by the build gate.
