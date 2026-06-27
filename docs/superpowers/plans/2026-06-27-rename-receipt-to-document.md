# Firmware Receipt → Document Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename every "receipt" reference in the `ditto-firmware` source to "document", matching the cloud rename, with zero protocol or behavior change.

**Architecture:** Pure mechanical rename in three reviewer-gated tasks: (1) the embedded test asset (file move + linker-symbol rename — the one build-breaking risk), (2) the `cloud_post_receipt` function + its callers + the multipart filename, (3) the remaining comment / log / UI-string sweep ending in a zero-"receipt" grep gate. No new tests — there is no unit harness for the cloud/render path; verification is the existing host suite (`tools/cfg-harness`) plus a clean `idf.py build` (which is what actually proves the asset/symbol rename links) plus the grep gate.

**Tech Stack:** ESP-IDF 5.5, C, LVGL, CMake `EMBED_FILES`, host test harness under `tools/cfg-harness`.

## Global Constraints

- **No protocol/behavior change.** `/api/ingest`, the multipart `name="file"` form field, the cloud command protocol, and all device behavior stay exactly as-is. This is a rename only.
- **Term:** `receipt` → `document` in all case/identifier variants (`receipt`→`document`, `Receipt`→`Document`, `cloud_post_receipt`→`cloud_post_document`, `_binary_test_receipt_png_*`→`_binary_test_document_png_*`, `test_receipt.png`→`test_document.png`, `filename="receipt.png"`→`filename="document.png"`).
- **Do NOT touch** `managed_components/`, `build/`, or git history.
- **Keep** the local C accessor variable names `test_png_start` / `test_png_end` (they contain no "receipt") and the `name="file"` multipart field name (no "receipt", and changing it is a protocol change).
- **Final grep gate (must be 0):** `grep -rinE "receipt" components main tools --include="*.c" --include="*.h" --include="CMakeLists.txt" | grep -v managed_components | grep -v "/build/"`
- **Verification commands** (used across tasks):
  - Host tests: `cd tools/cfg-harness && make test` → all pass.
  - Build: from repo root, `source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build` → `Project build complete.` with no errors.
- Work on branch `feat/rename-receipt-to-document` (already created, spec already committed there).

---

## File Map

| File | Change category |
|---|---|
| `components/assets/test_receipt.png` → `test_document.png` | asset move (Task 1) |
| `components/assets/CMakeLists.txt` | `EMBED_FILES` string (Task 1) |
| `components/assets/assets.c` | comment + 2 `asm()` symbol strings (Task 1) |
| `components/assets/include/assets.h` | comment (Task 1) |
| `components/cloud/include/cloud.h` | fn decl + comment (Task 2) |
| `components/cloud/cloud.c` | fn def + multipart filename (Task 2) |
| `main/render_job.c` | `cloud_post_receipt` call (Task 2) + comments/log (Task 3) |
| `main/app_state.c` | `cloud_post_receipt` call (Task 2) + comments/log (Task 3) |
| `main/app_main.c` | comments (Task 3) |
| `main/state.h` | comment (Task 3) |
| `components/ui/ui_settings.c` | 2 comments incl. UI subtitle string (Task 3) |
| `components/ui/include/ui.h` | comment (Task 3) |
| `components/devcfg/include/device_config.h` | comment (Task 3) |
| `components/render/render.c` | comment (Task 3) |
| `components/render/png_encode.c` | comment (Task 3) |
| `components/render/include/png_encode.h` | comment (Task 3) |
| `tools/png-test/test_png.c` | comment (Task 3) |

---

### Task 1: Rename the embedded test asset

**Files:**
- Rename: `components/assets/test_receipt.png` → `components/assets/test_document.png`
- Modify: `components/assets/CMakeLists.txt:3`
- Modify: `components/assets/assets.c:3-5`
- Modify: `components/assets/include/assets.h:5`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: the accessor functions in `assets.h`/`assets.c` keep their existing names and signatures (`const uint8_t *assets_test_png(void)` etc. — unchanged), so Task 2/3 callers are unaffected. Only the embedded-file name and its generated linker symbols change.

**Why this is first and isolated:** ESP-IDF's `EMBED_FILES` generates the linker symbols `_binary_<filename>_start/_end` from the filename. If the `asm("...")` strings in `assets.c` don't exactly match the new filename, the build fails at link. The `idf.py build` at the end of this task is the gate that proves the rename is correct.

- [ ] **Step 1: Move the asset file with git**

```bash
cd /Users/eren/Projects/ditto-firmware
git mv components/assets/test_receipt.png components/assets/test_document.png
```

- [ ] **Step 2: Update `EMBED_FILES` in the CMakeLists**

In `components/assets/CMakeLists.txt`, line 3 currently reads:

```cmake
                       EMBED_FILES "test_receipt.png" "fonts/ditto_sans.ttf" "fonts/ditto_sans_bold.ttf")
```

Change `"test_receipt.png"` → `"test_document.png"`:

```cmake
                       EMBED_FILES "test_document.png" "fonts/ditto_sans.ttf" "fonts/ditto_sans_bold.ttf")
```

- [ ] **Step 3: Update the symbol externs + comment in `assets.c`**

In `components/assets/assets.c`, lines 3-5 currently read:

```c
// Symbols produced by EMBED_FILES "test_receipt.png".
extern const uint8_t test_png_start[] asm("_binary_test_receipt_png_start");
extern const uint8_t test_png_end[]   asm("_binary_test_receipt_png_end");
```

Change to (keep the `test_png_start`/`test_png_end` local names; only the comment filename and the two `asm()` symbol strings change):

```c
// Symbols produced by EMBED_FILES "test_document.png".
extern const uint8_t test_png_start[] asm("_binary_test_document_png_start");
extern const uint8_t test_png_end[]   asm("_binary_test_document_png_end");
```

- [ ] **Step 4: Update the comment in `assets.h`**

In `components/assets/include/assets.h`, line 5 currently reads:

```c
// The bundled test receipt PNG (M3). Returns a pointer to the embedded bytes
```

Change `test receipt PNG` → `test document PNG`:

```c
// The bundled test document PNG (M3). Returns a pointer to the embedded bytes
```

- [ ] **Step 5: Build to verify the embed + symbols link**

Run from repo root:

```bash
source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build
```

Expected: build succeeds, ending in `Project build complete.`. A wrong symbol name would fail here with an `undefined reference to '_binary_test_document_png_start'` (or `_receipt_`) link error — if that happens, the `asm()` strings in Step 3 don't match the filename from Step 1/2; fix and rebuild.

- [ ] **Step 6: Commit**

```bash
git add components/assets/
git commit -m "refactor(assets): rename embedded test_receipt.png -> test_document.png"
```

---

### Task 2: Rename `cloud_post_receipt` → `cloud_post_document` + multipart filename

**Files:**
- Modify: `components/cloud/include/cloud.h:22,24`
- Modify: `components/cloud/cloud.c:122,135`
- Modify: `main/render_job.c:38`
- Modify: `main/app_state.c:144`

**Interfaces:**
- Consumes: nothing from Task 1 (independent).
- Produces: the renamed signature `int cloud_post_document(const uint8_t *png, size_t png_len, char *url_out, int url_cap);` — Task 3 touches comments in the same two caller files (`render_job.c`, `app_state.c`) but on different lines, so order between Task 2 and Task 3 is independent.

- [ ] **Step 1: Rename the declaration + fix the comment in `cloud.h`**

In `components/cloud/include/cloud.h`, line 22 currently reads:

```c
// On HTTP 201, copies the returned receipt URL into `url_out` (NUL-terminated,
```

and line 24:

```c
int cloud_post_receipt(const uint8_t *png, size_t png_len, char *url_out, int url_cap);
```

Change `returned receipt URL` → `returned document URL`, and `cloud_post_receipt` → `cloud_post_document`:

```c
// On HTTP 201, copies the returned document URL into `url_out` (NUL-terminated,
```

```c
int cloud_post_document(const uint8_t *png, size_t png_len, char *url_out, int url_cap);
```

- [ ] **Step 2: Rename the definition + multipart filename in `cloud.c`**

In `components/cloud/cloud.c`, line 122 currently reads:

```c
int cloud_post_receipt(const uint8_t *png, size_t png_len, char *url_out, int url_cap)
```

Change to:

```c
int cloud_post_document(const uint8_t *png, size_t png_len, char *url_out, int url_cap)
```

And line 135 currently reads (note: only the `filename=` value changes — the `name="file"` field name stays, per Global Constraints):

```c
        "Content-Disposition: form-data; name=\"file\"; filename=\"receipt.png\"\r\n"
```

Change `filename=\"receipt.png\"` → `filename=\"document.png\"`:

```c
        "Content-Disposition: form-data; name=\"file\"; filename=\"document.png\"\r\n"
```

- [ ] **Step 3: Update the caller in `render_job.c`**

In `main/render_job.c`, line 38 currently reads:

```c
    int status = cloud_post_receipt(png, png_len, url, sizeof(url));
```

Change to:

```c
    int status = cloud_post_document(png, png_len, url, sizeof(url));
```

- [ ] **Step 4: Update the caller in `app_state.c`**

In `main/app_state.c`, line 144 currently reads:

```c
    int status = cloud_post_receipt(png, len, url, sizeof(url));
```

Change to:

```c
    int status = cloud_post_document(png, len, url, sizeof(url));
```

- [ ] **Step 5: Build to verify the rename compiles + links**

Run from repo root:

```bash
source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build
```

Expected: build succeeds (`Project build complete.`). An implicit-declaration warning/error or `undefined reference to 'cloud_post_receipt'` means a caller or the header was missed; fix and rebuild.

- [ ] **Step 6: Commit**

```bash
git add components/cloud/ main/render_job.c main/app_state.c
git commit -m "refactor(cloud): rename cloud_post_receipt -> cloud_post_document + multipart filename"
```

---

### Task 3: Sweep remaining comments, logs, and the UI string; close the grep gate

**Files:**
- Modify: `main/render_job.c:16,42,46`
- Modify: `main/app_state.c:42,118,133,146,286,409,431,499`
- Modify: `main/app_main.c:30,31`
- Modify: `main/state.h:13`
- Modify: `components/ui/ui_settings.c:14,416`
- Modify: `components/ui/include/ui.h:39`
- Modify: `components/devcfg/include/device_config.h:71`
- Modify: `components/render/render.c:233`
- Modify: `components/render/png_encode.c:122`
- Modify: `components/render/include/png_encode.h:12`
- Modify: `tools/png-test/test_png.c:20`

**Interfaces:**
- Consumes: the renamed `cloud_post_document` from Task 2 (only relevant to the log lines that sit near the call — no code dependency).
- Produces: nothing new; this task closes the grep gate to 0.

These are all comment / log-string / one UI-subtitle-string edits — no signatures, no behavior. Each is a literal `receipt`→`document` (preserving case) on the quoted line. The one user-visible change is `ui_settings.c:416`.

- [ ] **Step 1: Edit `main/render_job.c` (3 lines)**

Line 16: `// receipt job is showing instead of clobbering it back to idle.` → `// document job is showing instead of clobbering it back to idle.`

Line 42: `        ESP_LOGI(TAG, "receipt ready: %s", url);` → `        ESP_LOGI(TAG, "document ready: %s", url);`

Line 46: `        // mid-receipt config refresh on the QR instead of clobbering it to idle.` → `        // mid-document config refresh on the QR instead of clobbering it to idle.`

- [ ] **Step 2: Edit `main/app_state.c` (8 lines)**

Line 42: `// Set true (escpos task) when a receipt arrives while run_settings_flow is mid-flow;` → `...when a document arrives while run_settings_flow is mid-flow;`

Line 118: `    // A receipt arriving while Settings is open must show the live QR. Signal any` → `    // A document arriving while Settings is open must show the live QR. Signal any`

Line 133: `// M3: upload the bundled test PNG and show the returned receipt URL as a QR.` → `// M3: upload the bundled test PNG and show the returned document URL as a QR.`

Line 146: `        ESP_LOGI(TAG, "receipt ready: %s", url);` → `        ESP_LOGI(TAG, "document ready: %s", url);`

Line 286: `// reboots — or until a receipt aborts the flow.` → `// reboots — or until a document aborts the flow.`

Line 409: `// the receipt screen when its countdown expires. Avoids extra cloud polls.` → `// the document screen when its countdown expires. Avoids extra cloud polls.`

Line 431: `        // A receipt that arrived while asleep (or a sleep that raced the wake)` → `        // A document that arrived while asleep (or a sleep that raced the wake)`

Line 499: `                    ui_set_countdown(s_cfg_buf[next]->qr_timeout_seconds);  // keep duration current for any receipt path` → `...// keep duration current for any document path`

- [ ] **Step 3: Edit `main/app_main.c` (2 lines)**

Line 30: `// Trigger/show_qr handler: display the receipt QR screen immediately.` → `// Trigger/show_qr handler: display the document QR screen immediately.`

Line 31: `// Mirrors the render_job receipt path (ui_set_qr_url + state + render).` → `// Mirrors the render_job document path (ui_set_qr_url + state + render).`

- [ ] **Step 4: Edit `main/state.h` (1 line)**

Line 13: `// ui_render_state(s_state) — preserves a live receipt/QR screen instead of` → `// ui_render_state(s_state) — preserves a live document/QR screen instead of`

- [ ] **Step 5: Edit `components/ui/ui_settings.c` (2 lines)**

Line 14: `// VISUAL: restyled to match the dReceipt staff mockups (PinScreen / MenuScreen).` → `// VISUAL: restyled to match the Ditto staff mockups (PinScreen / MenuScreen).` (drop the "dReceipt" token so no "receipt" remains; "Ditto" is the accurate referent.)

Line 416 (the user-visible Settings subtitle): `        { LV_SYMBOL_IMAGE,    "Test print",      "Send a sample receipt", UI_SET_TEST_PRINT },` → `        { LV_SYMBOL_IMAGE,    "Test print",      "Send a sample document", UI_SET_TEST_PRINT },`

- [ ] **Step 6: Edit `components/ui/include/ui.h` (1 line)**

Line 39: `// Arm the receipt-screen countdown to start at `seconds` on the next render of a` → `// Arm the document-screen countdown to start at `seconds` on the next render of a`

- [ ] **Step 7: Edit `components/devcfg/include/device_config.h` (1 line)**

Line 71: `    int      qr_timeout_seconds;  // receipt/QR auto-return timeout, 15..180` → `    int      qr_timeout_seconds;  // document/QR auto-return timeout, 15..180`

- [ ] **Step 8: Edit `components/render/render.c` (1 line)**

Line 233: `    // 1-bit PNG: receipt content is bilevel (white bg + black ink), so this is` → `    // 1-bit PNG: document content is bilevel (white bg + black ink), so this is`

- [ ] **Step 9: Edit `components/render/png_encode.c` (1 line)**

Line 122: `    // 0 = black. Receipt content is strictly bilevel (white bg + black ink) so` → `    // 0 = black. Document content is strictly bilevel (white bg + black ink) so`

- [ ] **Step 10: Edit `components/render/include/png_encode.h` (1 line)**

Line 12: `// at mid-gray (>=128 -> white). Lossless for bilevel receipt content and ~8x` → `// at mid-gray (>=128 -> white). Lossless for bilevel document content and ~8x`

- [ ] **Step 11: Edit `tools/png-test/test_png.c` (1 line)**

Line 20: `    // filled block — representative bilevel receipt content.` → `    // filled block — representative bilevel document content.`

- [ ] **Step 12: Run the grep gate — must be zero**

```bash
cd /Users/eren/Projects/ditto-firmware
grep -rinE "receipt" components main tools --include="*.c" --include="*.h" --include="CMakeLists.txt" | grep -v managed_components | grep -v "/build/"
```

Expected: **no output** (exit status 1). Any line printed is a missed hit — fix it before continuing.

- [ ] **Step 13: Run host tests**

```bash
cd tools/cfg-harness && make test
```

Expected: all tests pass (unchanged from before — this confirms the comment edits in `device_config.h` and the render/cfg paths didn't disturb anything).

- [ ] **Step 14: Final build**

Run from repo root:

```bash
source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build
```

Expected: `Project build complete.` (confirms the UI string + all comment edits compile cleanly).

- [ ] **Step 15: Commit**

```bash
git add main/ components/ui/ components/devcfg/ components/render/ tools/png-test/
git commit -m "refactor: sweep remaining receipt->document comments, logs, and Settings string"
```

---

## Self-Review

**Spec coverage:**
- Spec §A (identifiers + comments + logs): Task 2 (function rename) + Task 3 (all comments/logs in render_job, app_state, app_main, state.h, ui.h, device_config.h, render.c, png_encode.c/.h). ✅
- Spec §B (on-screen string): Task 3 Step 5, `ui_settings.c:416`. ✅
- Spec §C (multipart filename): Task 2 Step 2. ✅
- Spec §D (embedded asset): Task 1 (git mv + EMBED_FILES + assets.c symbols/comment + assets.h comment) and Task 3 Step 11 (`tools/png-test/test_png.c` comment — the spec's "update png-test reference"; it is comment-only, no file/symbol reference). ✅
- Testing gates (host tests + `idf.py build` + grep gate = 0): builds in Tasks 1, 2, 14; host tests + grep gate in Task 3. ✅
- No HIL (per spec decision). ✅

**Placeholder scan:** No TBD/TODO; every edit shows the exact before/after line. ✅

**Type/name consistency:** `cloud_post_document` signature identical to the original except the name; local accessor names `test_png_start/end` deliberately kept; symbols `_binary_test_document_png_start/_end` match the new filename `test_document.png`; multipart `name="file"` field deliberately unchanged. ✅

**Extra hit beyond the spec's "~32":** `ui_settings.c:14` ("dReceipt staff mockups") — a 33rd comment hit the spec's prose count rounded over; folded into Task 3 Step 5 so the grep gate can reach 0. Noted, not a scope change.
