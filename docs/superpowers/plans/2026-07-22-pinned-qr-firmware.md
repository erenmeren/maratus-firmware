# Pinned QR Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Device renders a persistent pinned QR (from the config `pin` block) whenever it would show idle; triggers override temporarily; `pin` commands ack + refresh config.

**Architecture:** Config is the sole source of truth (the `pin` command is just a refresh signal). New `DEV_PINNED` lifecycle state maps to the existing QR screen layout with the countdown suppressed by render-state. Three hardcoded "go home to idle" sites route through a `show_home()` helper.

**Tech Stack:** ESP-IDF 5.5 (P4 + C6), LVGL, cJSON; host tests via `tools/cfg-harness` (plain C, `make && ./run`).

**Spec:** `docs/superpowers/specs/2026-07-22-pinned-qr-firmware-design.md` — read it first.

## Global Constraints

- Repo: `/Users/eren/Projects/ditto-firmware`, branch `feat/pinned-qr` off `main`; commits only there.
- **ESP_IDF_VERSION must stay 5.5** (5.5.4 breaks esp_wifi_remote → boot mempool panic — see admin-repo memory `firmware-idf-version-sdio-trap`). Do not touch IDF/sdkconfig beyond `CONFIG_DITTO_FW_VERSION`.
- Gates per task: host suite `cd tools/cfg-harness && make && ./run` prints its final `ALL PASS`-style line; firmware `idf.py build` compiles clean with **no new warnings** (run `. $HOME/esp/esp-idf/bin/../export.sh 2>/dev/null || . $HOME/esp/esp-idf/export.sh` first if `idf.py` is absent; if the IDF env cannot be sourced, report BLOCKED).
- Old-cloud compatibility: `pin` absent/null ⇒ `pin_set=false` ⇒ behavior byte-identical to today (assert via host tests).
- `pin_url` buffer is 512 bytes (matches `dev_command_t.url`); parse must truncate-safely (`strlcpy`-style, never overflow).
- Style: match surrounding C (2-space indent, `s_` statics, short comments explaining constraints only).

---

### Task 1: devcfg — `pin` config block (TDD, host)

**Files:**
- Modify: `components/devcfg/include/device_config.h` (~line 62-93, `device_config_t`)
- Modify: `components/devcfg/cfg_parse.c` (`cfg_parse_json`, top-level section near the `device` block at ~line 171)
- Test: `tools/cfg-harness/test_cfg.c`

**Interfaces:**
- Produces: `cfg->pin_set` (bool), `cfg->pin_url[512]` on `device_config_t`, populated by `cfg_parse_json`. Consumed by Tasks 2/5.

- [ ] **Step 1: Failing host tests** — in `test_cfg.c`, following the file's existing assert style, add a `test_pin_block()` covering: (a) `{"pin":{"url":"https://x/menu"}}` → `pin_set==true`, url copied; (b) `"pin":null` → false; (c) `pin` absent → false; (d) `{"pin":{"url":""}}` → false; (e) 600-char URL → truncated to 511 chars + NUL, `pin_set==true`; (f) an existing full fixture still parses with `pin_set==false`. Build the JSON inline (like the file's other inline-JSON tests) or add a small fixture under `fixtures/`. Wire the new test into `main()`. Run `make && ./run` → compile error (missing fields) = RED.
- [ ] **Step 2: Add fields** to `device_config_t` (sibling of the `device`/`mqtt` structs):
```c
  // Pinned QR (top-level `pin` block): when set, the device shows pin_url as a
  // persistent QR instead of the idle screen. Cleared pin ⇒ pin_set=false.
  bool pin_set;
  char pin_url[512];
```
- [ ] **Step 3: Parse** in `cfg_parse_json`, next to the `device` block lookup:
```c
  cJSON *pin = cJSON_GetObjectItem(root, "pin");
  if (cJSON_IsObject(pin)) {
    cJSON *purl = cJSON_GetObjectItem(pin, "url");
    if (cJSON_IsString(purl) && purl->valuestring[0] != '\0') {
      strncpy(cfg->pin_url, purl->valuestring, sizeof(cfg->pin_url) - 1);
      cfg->pin_url[sizeof(cfg->pin_url) - 1] = '\0';
      cfg->pin_set = true;
    }
  }
```
(`memset` at the top of the function already zeroes both fields; match the file's actual string-copy idiom if it uses a helper.)
- [ ] **Step 4:** `make && ./run` → GREEN. `idf.py build` clean.
- [ ] **Step 5: Commit** — `feat(pin): parse config pin block into device_config_t`

---

### Task 2: devcfg — `DEV_PINNED` state machinery (TDD, host)

**Files:**
- Modify: `components/devcfg/include/dev_state.h` (enum, ~line 6-15)
- Modify: `components/devcfg/render_geom.c` (`screen_for_state`, ~line 33-45)
- Modify: `components/devcfg/sleep_policy.c` (`should_sleep`, ~line 3-10) + its header
- Test: `tools/cfg-harness/test_cfg.c`

**Interfaces:**
- Produces: `DEV_PINNED` enum value (append at the END of `dev_state_t` to avoid renumbering existing states); `screen_for_state(DEV_PINNED) == SCREEN_QR`; `dev_state_t home_state(bool pin_set)` (new pure fn, declared in `render_geom.h` next to `screen_for_state`); `should_sleep` treats `DEV_PINNED` like `DEV_IDLE`. Consumed by Tasks 3/5.

- [ ] **Step 1: Failing host tests** — add asserts: `screen_for_state(DEV_PINNED) == SCREEN_QR`; `home_state(true) == DEV_PINNED`; `home_state(false) == DEV_IDLE`; `should_sleep(...)` true for `DEV_PINNED` with elapsed timeout + enabled, false for `DEV_QR` (existing case still holds). RED (missing symbols).
- [ ] **Step 2: Implement** — enum append; `case DEV_PINNED: return SCREEN_QR;` in `screen_for_state`;
```c
dev_state_t home_state(bool pin_set) { return pin_set ? DEV_PINNED : DEV_IDLE; }
```
in `render_geom.c` + header decl; in `should_sleep` change the state check to `(state == DEV_IDLE || state == DEV_PINNED)`.
- [ ] **Step 3:** `make && ./run` GREEN; `idf.py build` clean (fix any `switch(dev_state_t)` exhaustiveness warnings by handling `DEV_PINNED` where the compiler flags them — report which).
- [ ] **Step 4: Commit** — `feat(pin): DEV_PINNED state + home_state helper + sleep parity`

---

### Task 3: ui — suppress countdown when rendering pinned

**Files:**
- Modify: `components/ui/ui.c` (`ui_render_state` ~line 629, `build_screen` object switch `case OBJ_COUNTDOWN:` ~line 575)

**Interfaces:**
- Consumes: `DEV_PINNED` (Task 2). Produces: rendering `DEV_PINNED` builds the QR screen without its countdown widget; all other states unchanged.

- [ ] **Step 1:** Add `static dev_state_t s_render_state = DEV_BOOT;` near `s_qr_url`; set it at the top of `ui_render_state` (inside the LVGL lock) before `build_screen` runs.
- [ ] **Step 2:** In `build_screen`'s switch: `case OBJ_COUNTDOWN: if (s_render_state != DEV_PINNED) render_countdown(...); break;` — keep the existing call args verbatim.
- [ ] **Step 3:** `idf.py build` clean. Grep check: `ui_render_state` is the only writer of `s_render_state`.
- [ ] **Step 4: Commit** — `feat(pin): pinned render suppresses countdown widget`

---

### Task 4: cloud — `pin` command = ack + config refresh

**Files:**
- Modify: `components/cloud/commands.c` (`dispatch_command`, ~line 24-56)

**Interfaces:**
- Consumes: the existing config-changed callback plumbing (same one `"config-changed"`/`"refresh"` uses). Produces: `type:"pin"` commands are acked `ok=true` on both transports and trigger a config re-fetch. Payload deliberately ignored (spec: config is the sole source of truth).

- [ ] **Step 1:** Read the `"config-changed"` branch; add a `"pin"` branch that does exactly the same (invoke the registered refresh callback if set, then `ack(cmd->command_id, true)`), with a one-line comment: `// pin: state lives in config (ETag-folded); refreshing is the whole job.`
- [ ] **Step 2:** If `tools/cfg-harness` covers `command_parse`/dispatch tables, extend it minimally; otherwise note "dispatch not host-covered (matches existing types)" in the report.
- [ ] **Step 3:** `idf.py build` clean. **Commit** — `feat(pin): ack pin command and refresh config`

---

### Task 5: main — `show_home()` + call-site rewiring

**Files:**
- Modify: `main/app_state.c` (sites: `idle_wait_or_qr_expiry` ~line 390-393, `boot_gate_fallback_offline` ~line 436-437, poll_task boot-gate-clear ~line 502-503, config-apply success path ~line 513-522)

**Interfaces:**
- Consumes: `home_state` (Task 2), `ui_set_qr_url`/`ui_render_state` (existing), live config access (`s_cfg_buf[s_cfg_live]`).
- Produces: all go-home paths show pin when set; config apply repaints pin changes within one poll cycle.

- [ ] **Step 1:** Add a static helper near the other helpers in `app_state.c`:
```c
// Route every "go home" to the pin when one is set. Reads the live config
// buffer; safe from poll_task (the only caller context).
static void show_home(void) {
  const device_config_t *cfg = s_cfg_buf[s_cfg_live];
  bool pinned = cfg && cfg->valid && cfg->pin_set;
  if (pinned) ui_set_qr_url(cfg->pin_url);
  s_state = home_state(pinned);
  ui_render_state(s_state);
}
```
(Adapt field/guard names to the actual buffer declarations; if `s_cfg_buf` pointers can be NULL before `app_state_run` seeds them, the `cfg &&` guard covers it.)
- [ ] **Step 2:** Replace the three `s_state = DEV_IDLE; ui_render_state(DEV_IDLE);` pairs (QR expiry, boot-gate fallback, boot-gate clear) with `show_home();`.
- [ ] **Step 3:** Config-apply success path: where it currently does `ui_render_state(s_state)` after `ui_set_config`, change to: if `s_state == DEV_IDLE || s_state == DEV_PINNED` → `show_home();` else `ui_render_state(s_state);` — with a comment that this is what repaints pin set/clear/URL-change live.
- [ ] **Step 4:** Verify by reading: entering `DEV_PINNED` must NOT force a display wake (`app_state_set_state` special-cases only `DEV_QR`/`DEV_PROCESSING`; `show_home` writes `s_state` directly like the sites it replaces — confirm it does not call `app_state_set_state`).
- [ ] **Step 5:** `idf.py build` clean. **Commit** — `feat(pin): go-home routes to pinned QR; config apply repaints pin changes`

---

### Task 6: Version + docs + full gates

- [ ] **Step 1:** `sdkconfig`: `CONFIG_DITTO_FW_VERSION="0.9.0"`; also bump the Kconfig default in `main/Kconfig.projbuild` to `"0.9.0"`.
- [ ] **Step 2:** `BUILD.md`: append a dated hardware-validation-log entry "Pinned QR (0.9.0) — pending HIL" listing what to verify (spec's HIL list).
- [ ] **Step 3:** Full gates: `cd tools/cfg-harness && make && ./run` (all pass) + `idf.py build` (clean). Paste both tails in the report.
- [ ] **Step 4: Commit** — `build: bump fw to 0.9.0 + BUILD.md pinned-QR entry`

HIL (flash b580, exercise spec's HIL list) happens after review, controller/user-driven — not part of this plan's tasks.
