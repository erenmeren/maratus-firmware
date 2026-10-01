# Strip the Dead Receipt/Print Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the dead ESC/POS → render → `/api/ingest` receipt/print pipeline from the trigger-only firmware, keeping the live trigger→QR path intact.

**Architecture:** Remove in dependency order so the source tree stays self-consistent at each step: sever the live entry points first (boot listener, tap-to-test, "Test print"), then the now-orphaned `cloud_post_document` + its Kconfig, then delete the 5 unreferenced components, then the dead dev tools. Each unreferenced component still compiles until its own deletion task, so an `idf.py build` would pass after every task.

**Tech Stack:** ESP32-P4 / ESP-IDF 5.5, C, CMake, LVGL. Host tests via `tools/cfg-harness` (plain `cc`).

## Global Constraints

- **KEEP the live trigger→QR path untouched:** `components/cloud/cloud.c` (`cloud_get_commands`/`cloud_ack_command`/`cloud_set_trigger_cb`/`cloud_trigger_handler`), `components/cloud/commands.c`, `components/cloud/command_parse.c`, `main/app_main.c:on_trigger_show_qr`, `components/ui/ui.c` (`render_qr`/`ui_set_qr_url`/`DEV_QR`/countdown), `main/app_state.c:idle_wait_or_qr_expiry` and the poll loop.
- **KEEP the `dev_state_t` enum** (all values) — no enum pruning (cross-repo concern). `DEV_PROCESSING`/`DEV_UPLOADING` simply become unreachable.
- **KEEP shared infra:** OTA, net/Wi-Fi, `devcfg`/`assetcache`, `appcfg` (minus `appcfg_roll_width`), `assets` fonts + `font_cache`, the `ui_consume_tap`/`ui_consume_swipeup` mechanism.
- **This environment has NO `idf.py`** — implementers CANNOT compile. Verify every task with: (1) grep gates (named per task), (2) `cd tools/cfg-harness && make test` stays green (covers the live `trigger/show_qr` parser), (3) a manual scan that no `CMakeLists.txt` REQUIRES/EMBED, no `#include`, and no call site references a removed symbol. **The authoritative `idf.py build` (target esp32p4) + HIL flash-and-confirm are the USER's gate before merge** — state this in the final handoff.
- Commit each task separately. Co-author trailer on every commit: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

### Task 1: Sever the live entry points + delete render_job + Test-print + test asset

Remove everything that still *invokes* the dead pipeline. After this task, `cloud_post_document` and the 5 receipt components are unreferenced (but still present → still compile).

**Files:**
- Modify: `main/app_main.c`, `main/app_state.c`, `main/CMakeLists.txt`
- Delete: `main/render_job.c`, `main/render_job.h`
- Modify: `components/ui/include/ui.h`, `components/ui/ui_settings.c`
- Modify: `components/assets/assets.c`, `components/assets/include/assets.h`, `components/assets/CMakeLists.txt`
- Delete: `components/assets/test_document.png`

- [ ] **Step 1: `main/app_main.c` — remove dead includes + calls.**
  - Delete the three includes: `#include "escpos_server.h"` (line 7), `#include "render_job.h"` (8), `#include "render.h"` (9).
  - Delete the line `render_set_roll_width(appcfg_roll_width());` (line ~101).
  - Delete the two lines `escpos_server_start(render_job_handle);` and the following `ESP_LOGI(TAG, "print server up on :9100");` (~158-159), plus the `// 5) ESC/POS print-job listener on TCP:9100.` comment above them.
  - Reword the stale comment at ~line 31 `// Mirrors the render_job document path (ui_set_qr_url + state + render).` to `// Live trigger handler: show a QR of the URL and enter the QR screen.` (the function it documents, `on_trigger_show_qr`, is KEPT).

- [ ] **Step 2: `main/app_state.c` — remove run_test_ingest + its call sites + stale comments.**
  - Delete the entire `run_test_ingest(void)` function (the `static void run_test_ingest(void) { ... }` block, ~lines 134-156, including its `// M3: upload the bundled test PNG ...` comment above it).
  - Delete the `case UI_SET_TEST_PRINT:` block (`close_settings(scr); run_test_ingest(); return;`, ~357-360).
  - In the poll-loop tap block (~465-469), delete the inner line `if (now_ms() >= s_wake_guard_until && net_is_connected()) run_test_ingest();` and its comment `// A tap that merely woke the screen must not also trigger ingest.`, leaving the tap consumed and the loop continuing:

```c
        if (ui_consume_tap()) {
            continue;
        }
```

  - Update stale comments referencing the removed ESC/POS task / render_job at lines ~29, ~42, ~52, ~119-120 so they no longer describe the escpos task (e.g. line 29 `Written by poll_task and by render_job (the ESC/POS task) via app_state_set_state;` → `Written by poll_task via app_state_set_state;`). Keep `app_state_set_state` itself (exported in `state.h`; harmless if now single-writer).

- [ ] **Step 3: Delete `main/render_job.c` + `main/render_job.h`.** Run: `git rm main/render_job.c main/render_job.h`.

- [ ] **Step 4: `main/CMakeLists.txt` — drop render_job source + escpos/render requires.** Replace the file with:

```cmake
idf_component_register(SRCS "app_main.c" "app_state.c" "wifi_setup.c"
                       INCLUDE_DIRS "."
                       REQUIRES ui net cloud ota appcfg assets devcfg assetcache nvs_flash time_sync esp_wifi)
```

- [ ] **Step 5: `components/ui` — remove the "Test print" menu item.**
  - `components/ui/include/ui.h`: delete the enum line `UI_SET_TEST_PRINT,  // user tapped "Test Print"` (~line 93).
  - `components/ui/ui_settings.c`: delete the menu row `{ LV_SYMBOL_IMAGE, "Test print", "Send a sample document", UI_SET_TEST_PRINT },` (~line 416).

- [ ] **Step 6: `components/assets` — remove the embedded test PNG.**
  - `components/assets/assets.c`: delete the two `extern const uint8_t test_png_*` decls (~4-5), their `// Symbols produced by EMBED_FILES "test_document.png".` comment (~3), and the `assets_test_png()` function (~7-9).
  - `components/assets/include/assets.h`: delete `const uint8_t *assets_test_png(size_t *len);` (~7).
  - `components/assets/CMakeLists.txt`: change `EMBED_FILES "test_document.png" "fonts/maratus_sans.ttf" "fonts/maratus_sans_bold.ttf"` to `EMBED_FILES "fonts/maratus_sans.ttf" "fonts/maratus_sans_bold.ttf"` (drop only the PNG; KEEP both fonts).
  - `git rm components/assets/test_document.png`.

- [ ] **Step 7: Grep gate.** Run:

```
grep -rInE "escpos_server|render_job|run_test_ingest|render_set_roll_width|UI_SET_TEST_PRINT|assets_test_png|test_document|test_png_" main components/ui components/assets
```

Expected: **no output**. (If any hit remains, it's a missed reference — fix it.)

- [ ] **Step 8: Host tests stay green.** Run: `cd tools/cfg-harness && make test`. Expected: PASS (the live `test_trigger_show_qr` case is unaffected).

- [ ] **Step 9: Commit.**

```bash
git add -A
git commit -m "refactor(fw): remove dead receipt entry points (escpos listener, tap/Test-print ingest, render_job)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Remove `cloud_post_document` + the roll-width Kconfig/getter

Now that `render_job.c` and `run_test_ingest` are gone, `cloud_post_document` and `appcfg_roll_width` have no callers.

**Files:** `components/cloud/cloud.c`, `components/cloud/include/cloud.h`, `main/Kconfig.projbuild`, `components/appcfg/appcfg.c`, `components/appcfg/include/appcfg.h`

- [ ] **Step 1: `components/cloud/include/cloud.h` — remove the declaration.** Delete the `cloud_post_document` declaration (~line 24) and its `// POST /api/ingest with a PNG image ...` comment (~21-23).

- [ ] **Step 2: `components/cloud/cloud.c` — remove the function.** Delete the entire `int cloud_post_document(const uint8_t *png, size_t png_len, char *url_out, int url_cap) { ... }` function (~lines 122 through its closing brace, ~192). Read the file to find the exact closing brace; remove nothing else (leave `cloud_get_commands`/`cloud_ack_command` and shared HTTP helpers intact). If any `static` helper becomes used only by the removed function, remove it too (grep its name to confirm zero remaining callers first).

- [ ] **Step 3: `main/Kconfig.projbuild` — remove the roll-width config.** Delete the `config MARATUS_ROLL_WIDTH` block (starts ~line 34: the `config`, its `int` prompt, `default`, `range`, and `help` lines — the whole stanza).

- [ ] **Step 4: `components/appcfg` — remove the getter.**
  - `components/appcfg/appcfg.c`: delete `int appcfg_roll_width(void) { return CONFIG_MARATUS_ROLL_WIDTH; }` (~line 32).
  - `components/appcfg/include/appcfg.h`: delete `int appcfg_roll_width(void);` (~line 10).

- [ ] **Step 5: Grep gate.** Run:

```
grep -rInE "cloud_post_document|/api/ingest|MARATUS_ROLL_WIDTH|appcfg_roll_width|roll_width" main components
```

Expected: **no output** (the `POST /ingest` log strings inside the deleted function are gone with it).

- [ ] **Step 6: Host tests stay green.** Run: `cd tools/cfg-harness && make test`. Expected: PASS.

- [ ] **Step 7: Commit.**

```bash
git add -A
git commit -m "refactor(fw): remove cloud_post_document (/api/ingest) + roll-width config

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Delete the 5 dead receipt components

`escpos`, `render`, `barcode`, `font8x8`, `qrcodegen` are now unreferenced by any kept component (main's REQUIRES was updated in Task 1; `render`'s own REQUIRES pulled in barcode/font8x8/qrcodegen).

**Files (delete whole dirs):** `components/escpos/`, `components/render/`, `components/barcode/`, `components/font8x8/`, `components/qrcodegen/`

- [ ] **Step 1: Confirm no kept component REQUIRES them.** Run:

```
grep -rInE "\b(escpos|render|barcode|font8x8|qrcodegen)\b" $(git ls-files 'components/*/CMakeLists.txt' 'main/CMakeLists.txt' | grep -vE 'components/(escpos|render|barcode|font8x8|qrcodegen)/')
```

Expected: **no output** (nothing kept still requires them).

- [ ] **Step 2: Delete the components.** Run:

```bash
git rm -r components/escpos components/render components/barcode components/font8x8 components/qrcodegen
```

- [ ] **Step 3: Grep gate.** Run:

```
grep -rInE "escpos|render_ops_to_png|png_encode|font8x8|qrcodegen|code128|render\.h" main components
```

Expected: **no output**. (Note: this must NOT match `ui_render_state`/`render_qr`/`render_countdown` in `components/ui` — those are the kept LVGL screen renderers, unrelated to the deleted `render` component. If the grep flags them, they are false positives from the bare word `render`; confirm each hit is a `ui`-owned symbol and not an include of the deleted `render.h` / a call into the deleted component.)

- [ ] **Step 4: Host tests stay green.** Run: `cd tools/cfg-harness && make test`. Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "refactor(fw): delete dead receipt components (escpos, render, barcode, font8x8, qrcodegen)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Delete dead dev tools + final sweep

**Files (delete whole dirs):** `tools/escpos-harness/`, `tools/png-test/`. Modify docs referencing the removed pipeline (`BUILD.md`, `docs/bsp-notes.md`) if they describe ESC/POS printing / roll width / test-print.

- [ ] **Step 1: Delete the dead tools.** Run: `git rm -r tools/escpos-harness tools/png-test`.

- [ ] **Step 2: Sweep docs.** Grep `grep -rInE "escpos|receipt|roll.?width|/api/ingest|test print|Test print" BUILD.md docs README* 2>/dev/null`. For each hit that documents the removed pipeline (not historical spec/plan files under `docs/superpowers/`), update or remove the sentence. Leave the `docs/superpowers/specs|plans/` history untouched (those are dated records). If `BUILD.md` documents the `:9100` listener / "Test print" as a usage step, remove that step.

- [ ] **Step 3: Final whole-repo grep gate.** Run:

```
grep -rInE "escpos|render_job|cloud_post_document|/api/ingest|run_test_ingest|UI_SET_TEST_PRINT|MARATUS_ROLL_WIDTH|assets_test_png|png_encode|font8x8|qrcodegen|barcode" -- ':!docs/superpowers/*'
```

Expected: **no output** outside the `docs/superpowers/` spec+plan history. (Any residual hit in code/tools/build is a miss — fix it.)

- [ ] **Step 4: Host tests stay green.** Run: `cd tools/cfg-harness && make test`. Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "chore(fw): remove dead escpos/png dev tools + sweep receipt docs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final handoff (USER — cannot be done in this environment)

Before merging the branch, the user must run the authoritative gate:

1. **Compile:** `idf.py set-target esp32p4 && idf.py build` — must succeed (this is the real check that no dangling reference survived the grep gates). If it fails, the error names the missed reference; fix + re-grep.
2. **HIL:** flash + confirm on hardware:
   - A cloud `trigger`/`show_qr` command still renders a QR and auto-returns to idle.
   - The device no longer opens TCP `:9100` (e.g. `nc <device-ip> 9100` refuses).
   - Screen taps and the (now-absent) "Test print" settings item do nothing unexpected; swipe-up Settings, idle clock, OTA still work.

## Self-review notes

- **Spec coverage:** remove escpos/render/barcode/font8x8/qrcodegen → Task 3; render_job + entry points (boot listener, tap, Test-print) → Task 1; cloud_post_document → Task 2; test_document.png asset → Task 1; ROLL_WIDTH Kconfig + appcfg getter → Task 2; dead tools → Task 4; keep live path + dev_state_t enum + shared infra → Global Constraints; verification boundary (no idf.py; user does build+HIL) → Global Constraints + Final handoff.
- **Placeholder scan:** none — every step names exact files/symbols/lines + a grep gate with expected output. Deletion steps show the exact `git rm`; the two full-file edits (main/CMakeLists.txt) show the complete result.
- **Ordering/consistency:** entry points (1) before orphaned impl (2) before component deletion (3) before tools (4); each leaves the tree buildable (unreferenced components still compile until Task 3). The Task 3 grep note pre-empts the `render` false-positive against the kept `ui` renderers.
