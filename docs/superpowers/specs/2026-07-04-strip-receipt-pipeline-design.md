# Strip the dead receipt/print pipeline — design

**Date:** 2026-07-04
**Status:** Draft — awaiting user review
**Repo:** maratus-firmware (ESP32-P4 / ESP-IDF 5.5)
**Context:** Follow-up #4 from the maratus-admin trigger-only pivot. The cloud already stopped ingesting/hosting documents; the device is driven solely by the `trigger`/`show_qr` command (shows a QR of a caller-supplied URL). This removes the firmware's now-dead receipt/print rendering + ingest pipeline.

## Problem

The device firmware still contains the entire OLD "render a receipt → upload it → show its URL" pipeline (ESC/POS parse → grayscale framebuffer → PNG → `POST /api/ingest`). It is **not just product-dead — it is broken in production**: `cloud_post_document()` posts to `/api/ingest`, which was removed from the cloud in the trigger-only teardown, so every path that reaches it now 404s.

Three entry points still invoke this dead pipeline in the shipped binary:
1. `escpos_server_start()` on boot (`main/app_main.c`) — opens a TCP `:9100` ESC/POS listener.
2. Tap-to-test — a screen tap (`ui_consume_tap()` in `main/app_state.c`'s poll task) calls `run_test_ingest()`.
3. Settings → "Test print" (`UI_SET_TEST_PRINT`) — also calls `run_test_ingest()`.

The **live** trigger→QR path is entirely separate (it uses LVGL's `lv_qrcode` widget, not the dead `render`/`qrcodegen` components) and must not be touched.

## Decisions (best-judgment defaults — flagged for review)

1. **Delete the test hook outright** (not repurpose it). `run_test_ingest`, tap-to-ingest, the "Test print" menu item, and the `:9100` listener are all removed. Rationale: the hook is broken (404s), the device is cloud-driven, and an on-device QR self-test is a separate feature (YAGNI). *(ASSUMPTION — if you want a bench self-test, we'd instead rewire "Test print" to call `on_trigger_show_qr(<test URL>)` locally; say so and the plan changes.)*
2. **Keep the `dev_state_t` enum** intact. `DEV_PROCESSING`/`DEV_UPLOADING`/etc. become unreachable in firmware once the dead callers are gone, but the admin device-config UI + `/api/device/config` still reference those screens. Pruning the enum is a cross-repo concern; keeping it makes this a **firmware-only** removal. *(ASSUMPTION — flagged.)*

## Scope — REMOVE

**Local components (delete in full):**
- `components/escpos/` — TCP `:9100` job server + ESC/POS byte-stream parser.
- `components/render/` — `render_ops_to_png`, `png_encode` (receipt framebuffer → PNG).
- `components/barcode/` — Code128 rasterization (receipt barcodes only).
- `components/font8x8/` — bitmap font (only used by `render.c` to rasterize ESC/POS text).
- `components/qrcodegen/` — header-only CMake shim used only by the dead `render.c` (the live QR screen uses LVGL's `lv_qrcode` directly).

**`main/`:**
- `main/render_job.c` + `main/render_job.h` (the ESC/POS job callback: parse → render → `cloud_post_document` → show QR).
- `run_test_ingest()` in `main/app_state.c` and its two call sites (the tap handler in the poll task, and the "Test print" settings handler ~lines 357-360).
- The `escpos_server_start(render_job_handle)` call + include in `main/app_main.c`.
- `CONFIG_MARATUS_ROLL_WIDTH` in `main/Kconfig.projbuild` ("Receipt roll width in dots").
- `escpos` and `render` from the `REQUIRES` list in `main/CMakeLists.txt`.

**`components/cloud/`:**
- `cloud_post_document()` (+ its `/api/ingest` request code) in `cloud.c` and its declaration in `cloud.h`. Verified: its only callers are `render_job.c` and `run_test_ingest` (both removed above).

**`components/ui/`:**
- The `UI_SET_TEST_PRINT` menu item in `ui_settings.c` and its enum value in `ui.h`.
- The tap mechanism (`ui_consume_tap`/`s_tap_requested`) if it becomes fully unused after removing the tap→ingest call site (grep to confirm; remove if orphaned, else leave).

**`components/assets/`:**
- `test_document.png` + its `EMBED_FILES` entry in `components/assets/CMakeLists.txt` + the `asm("_binary_test_document_png_*")` externs + `assets_test_png()` in `assets.c`/`assets.h`. **Keep** the `.ttf` fonts (shared UI fonts).

**`tools/`:**
- `tools/escpos-harness/` (Node fake-POS sender) and `tools/png-test/` (host test for the dead `png_encode.c`) — both dead-pipeline-only.

## Scope — KEEP (do not touch)

- **Live trigger→QR path:** `components/cloud/cloud.c` (`cloud_get_commands`, `cloud_ack_command`, `cloud_set_trigger_cb`, `cloud_trigger_handler`), `components/cloud/commands.c` (`commands_handle_body`), `components/cloud/command_parse.c` (`command_trigger_show_qr_url`), `main/app_main.c` (`on_trigger_show_qr`), `components/ui/ui.c` (`render_qr`, `ui_set_qr_url`, `DEV_QR`, countdown), `main/app_state.c` (`idle_wait_or_qr_expiry`, the poll task minus the tap→ingest line).
- **Shared infra:** the cloud command-dispatch + callback-registration pattern, config-changed + firmware-update handlers, OTA, Wi-Fi/net, `devcfg`/`assetcache`, `assets` fonts + `font_cache`.
- **`dev_state_t` enum** (all values) and the config-driven screen rendering for them.

## Verification — and an honest boundary

**What I can verify in this environment (no ESP-IDF toolchain / `idf.py` is NOT on PATH here):**
- `tools/cfg-harness` host unit tests (`cd tools/cfg-harness && make test`, plain `cc`) — they cover the LIVE `trigger/show_qr` parser (`command_parse.c`) and `devcfg`/`assetcache`. Must stay green (proves the kept path compiles + parses).
- **Grep gates** after removal: `grep -rinE "escpos|render_job|run_test_ingest|cloud_post_document|roll_width|test_document|png_encode|font8x8|qrcodegen|code128" main components tools` returns only intentional residue (ideally nothing). Every deleted component name absent from all `CMakeLists.txt` `REQUIRES`/`EMBED_FILES`; every deleted header absent from all `#include`s; every deleted function absent from all call sites.
- Static consistency review of `main/CMakeLists.txt`, `components/*/CMakeLists.txt`, and includes.

**What is YOURS (I cannot do it here):**
- The authoritative **`idf.py build`** (target esp32p4) — the real compile gate. Because I can't compile, a dangling reference could survive my grep gates; your build is the backstop before merge.
- **HIL:** flash the board and confirm (a) a cloud `trigger`/`show_qr` still renders a QR and auto-returns, (b) the device no longer opens `:9100`, (c) taps and the (now-absent) "Test print" item no longer do anything unexpected, (d) normal idle/clock/OTA still work.

This split will be stated in the plan: each task ends with grep-clean + cfg-harness green; the branch is NOT considered done until you run `idf.py build` + HIL.

## Approach

Remove in dependency order so each step leaves the *source tree* self-consistent (even though I can't compile between steps): first the call sites/entry points in `main/` (app_main boot call, app_state tap + test-ingest, render_job), then `cloud_post_document`, then the UI "Test print" item + asset, then delete the 5 now-unreferenced components + their `CMakeLists` REQUIRES, then Kconfig + tools. A final grep sweep + cfg-harness run, then hand to you for `idf.py build` + HIL.

## Non-goals

- No change to the live trigger→QR behavior or the device↔cloud command/ack protocol.
- No `dev_state_t` enum pruning (cross-repo; deferred).
- No cloud-side changes (the cloud already removed `/api/ingest`).
- No new bench self-test feature (unless Decision 1 is overridden).
