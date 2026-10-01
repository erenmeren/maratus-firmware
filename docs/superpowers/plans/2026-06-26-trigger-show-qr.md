# Firmware Trigger Execution (show_qr + ack) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On a `trigger`/`show_qr` command, the device displays a QR of `payload.url` on the existing `DEV_QR` screen (auto-returning after `qrTimeoutSeconds`) and acks it — `ok=true` when displayed (cloud settles = charged), `ok=false` for invalid/missing url (cloud releases = refund).

**Architecture:** Firmware-only (maratus-firmware). A pure cJSON helper extracts the `show_qr` url (host-testable). The cloud command dispatcher gains a `trigger` branch that invokes an app-registered callback (mirroring the `config-changed` callback pattern, keeping the cloud component decoupled from app/ui state) and acks. The callback reuses the receipt path `ui_set_qr_url(url); app_state_set_state(DEV_QR); ui_render_state(DEV_QR)`.

**Tech Stack:** ESP-IDF 5.5 / C / cJSON / LVGL; host unit tests via `tools/cfg-harness` (no ESP-IDF).

## Global Constraints

- **Charge on display:** ack `ok=true` once the QR is shown; `ok=false` for missing/empty url, non-`show_qr` action, or no registered callback. (Cloud Spec A settles on ack ok=true, releases on ok=false.)
- **Reuse `DEV_QR`** + the existing `qrTimeoutSeconds` countdown auto-return (`idle_wait_or_qr_expiry`). No new screen state.
- **Cloud is unchanged.** The command already carries `{type:"trigger", action:"show_qr", payload:{url}}`; ack is `POST /api/device/commands/ack {commandId, ok}` via `cloud_ack_command(id, ok)`.
- **Decoupling:** the cloud component must not depend on app_state. The trigger callback is registered by the app via a `cloud_set_trigger_cb`/`cloud_trigger_handler` pair, exactly like `cloud_set_config_changed_cb`/`cloud_config_changed_handler` (cloud.h:52-54).
- **Device only ENCODES the url into a QR** — never fetches it (no SSRF).
- **Build:** `source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build`. **Host tests:** `cd tools/cfg-harness && make test`. **Console:** `python3 tools/read-console.py 30` (NOT `idf.py monitor`).
- Branch: `feat/trigger-show-qr` (already created). Commit footer: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

## File Structure

- `components/cloud/command_parse.c` + `components/cloud/include/command_parse.h` — pure `command_trigger_show_qr_url(cmd)` (cJSON-only, host-testable). (Task 1)
- `tools/cfg-harness/test_cfg.c` + `tools/cfg-harness/Makefile` — host test for the helper. (Task 1)
- `components/cloud/include/cloud.h` + `components/cloud/cloud.c` — `cloud_set_trigger_cb` / `cloud_trigger_handler`. (Task 2)
- `components/cloud/commands.c` — `trigger` dispatch branch using the helper + handler. (Task 2)
- App startup (where `cloud_set_config_changed_cb(...)` is already called — find it; likely `main/app_main.c` or `main/app_state.c`) — define + register the trigger callback. (Task 2)
- HIL verification. (Task 3)

---

## Task 1: Pure `show_qr` url parser + host test

**Files:**
- Create: `components/cloud/command_parse.c`, `components/cloud/include/command_parse.h`
- Modify: `tools/cfg-harness/test_cfg.c`, `tools/cfg-harness/Makefile`

**Interfaces:**
- Produces: `const char *command_trigger_show_qr_url(const cJSON *cmd)` — returns the `payload.url` string iff `cmd.action == "show_qr"` and `url` is a non-empty string; else `NULL`. (Pure; depends only on cJSON. Does NOT check `type` — the dispatcher matches `type=="trigger"` before calling.)

- [ ] **Step 1: Write the header**

`components/cloud/include/command_parse.h`:
```c
#pragma once
#include "cJSON.h"

// Pure: if `cmd` has action=="show_qr" and a non-empty payload.url string, return
// that url (pointer into the cJSON tree, valid until cJSON_Delete). Else NULL.
const char *command_trigger_show_qr_url(const cJSON *cmd);
```

- [ ] **Step 2: Add the failing host test + wire the Makefile**

In `tools/cfg-harness/Makefile`, add `../../components/cloud/command_parse.c` to the `SRCS` list and `-I../../components/cloud/include` to the include flags (match the existing `-I` style).

In `tools/cfg-harness/test_cfg.c`: add `#include "command_parse.h"` and a test, and call it from `main()` (mirror the existing `test_*` + `printf("... OK")` pattern):
```c
static void test_trigger_show_qr(void) {
    cJSON *ok = cJSON_Parse("{\"type\":\"trigger\",\"action\":\"show_qr\",\"payload\":{\"url\":\"https://e.co/r/x\"}}");
    assert(ok);
    const char *u = command_trigger_show_qr_url(ok);
    assert(u && strcmp(u, "https://e.co/r/x") == 0);
    cJSON_Delete(ok);

    cJSON *nourl = cJSON_Parse("{\"action\":\"show_qr\",\"payload\":{}}");
    assert(command_trigger_show_qr_url(nourl) == NULL);
    cJSON_Delete(nourl);

    cJSON *empty = cJSON_Parse("{\"action\":\"show_qr\",\"payload\":{\"url\":\"\"}}");
    assert(command_trigger_show_qr_url(empty) == NULL);
    cJSON_Delete(empty);

    cJSON *other = cJSON_Parse("{\"action\":\"print\",\"payload\":{\"url\":\"https://e.co\"}}");
    assert(command_trigger_show_qr_url(other) == NULL);
    cJSON_Delete(other);

    cJSON *nopayload = cJSON_Parse("{\"action\":\"show_qr\"}");
    assert(command_trigger_show_qr_url(nopayload) == NULL);
    cJSON_Delete(nopayload);

    printf("test_trigger_show_qr OK\n");
}
```
Add `test_trigger_show_qr();` to `main()`.

- [ ] **Step 3: Run the host tests to verify failure**

Run: `cd tools/cfg-harness && make test`
Expected: link/compile failure (`command_trigger_show_qr_url` undefined) — the helper doesn't exist yet.

- [ ] **Step 4: Implement the helper**

`components/cloud/command_parse.c`:
```c
#include "command_parse.h"
#include <string.h>

const char *command_trigger_show_qr_url(const cJSON *cmd) {
    if (!cmd) return NULL;
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "action"));
    if (!action || strcmp(action, "show_qr") != 0) return NULL;
    const cJSON *payload = cJSON_GetObjectItem((cJSON *)cmd, "payload");
    if (!payload) return NULL;
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)payload, "url"));
    if (!url || url[0] == '\0') return NULL;
    return url;
}
```

- [ ] **Step 5: Run the host tests to verify pass**

Run: `cd tools/cfg-harness && make test`
Expected: `test_trigger_show_qr OK` and all other tests still pass.

- [ ] **Step 6: Commit**

```bash
git add components/cloud/command_parse.c components/cloud/include/command_parse.h tools/cfg-harness/test_cfg.c tools/cfg-harness/Makefile
git commit -m "feat(cmd): pure command_trigger_show_qr_url parser + host test

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Trigger dispatch + callback plumbing + app registration

**Files:**
- Modify: `components/cloud/include/cloud.h`, `components/cloud/cloud.c`
- Modify: `components/cloud/commands.c`
- Modify: the app startup file that calls `cloud_set_config_changed_cb(...)` (find it: `grep -rn "cloud_set_config_changed_cb(" main components`)

**Interfaces:**
- Consumes: `command_trigger_show_qr_url` (Task 1); existing `cloud_ack_command`, `ui_set_qr_url`, `app_state_set_state`, `ui_render_state`.
- Produces: `cloud_set_trigger_cb(void (*cb)(const char *url))` + `void (*cloud_trigger_handler(void))(const char *url)`.

- [ ] **Step 1: Add the trigger-callback setter/getter to cloud.h**

In `components/cloud/include/cloud.h`, after the firmware-update cb pair (around line 60), add:
```c
// Trigger callback: invoked with the show_qr url when a trigger command arrives.
// Registered by the app so the cloud component stays decoupled from app/ui state.
void cloud_set_trigger_cb(void (*cb)(const char *url));
void (*cloud_trigger_handler(void))(const char *url);
```

- [ ] **Step 2: Implement them in cloud.c**

In `components/cloud/cloud.c`, mirror the existing `s_config_changed_cb`/`cloud_set_config_changed_cb`/`cloud_config_changed_handler` implementations (find them and copy the shape):
```c
static void (*s_trigger_cb)(const char *url) = NULL;
void cloud_set_trigger_cb(void (*cb)(const char *url)) { s_trigger_cb = cb; }
void (*cloud_trigger_handler(void))(const char *url) { return s_trigger_cb; }
```

- [ ] **Step 3: Add the `trigger` branch to the dispatcher**

In `components/cloud/commands.c`: add `#include "command_parse.h"` at the top. Add a branch before the final `else` (line 54), mirroring the existing handler style:
```c
        } else if (strcmp(type, "trigger") == 0) {
            const char *url = command_trigger_show_qr_url(cmd);
            void (*tcb)(const char *) = cloud_trigger_handler();
            if (url && tcb) {
                tcb(url);
                cloud_ack_command(id, true);   // displayed -> cloud settles (charged)
            } else {
                cloud_ack_command(id, false);  // bad/missing url or no handler -> cloud releases
            }
        } else {
```
(The `cmd` cJSON node is already in scope in the loop.)

- [ ] **Step 4: Define + register the app-side trigger callback**

In the app file that already registers the other cloud callbacks (the one calling `cloud_set_config_changed_cb(...)`), add a handler that reuses the receipt path and register it. It needs `ui.h` (`ui_set_qr_url`, `ui_render_state`) and `app_state` (`app_state_set_state`) — both already used in that area. Example:
```c
static void on_trigger_show_qr(const char *url) {
    ui_set_qr_url(url);
    app_state_set_state(DEV_QR);   // wakes display, aborts a settings flow, sets s_state
    ui_render_state(DEV_QR);       // paint the QR screen (+ countdown auto-return)
}
// ...alongside the existing cloud_set_config_changed_cb(...) registration:
cloud_set_trigger_cb(on_trigger_show_qr);
```
(If `go(dev_state_t)` is already a shared helper reachable here, call `ui_set_qr_url(url); go(DEV_QR);` instead — match whatever the receipt flow uses in this file.)

- [ ] **Step 5: Build the firmware**

Run: `source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py build`
Expected: compiles clean; `command_parse.c` is picked up by the `components/cloud` CMake glob (confirm it builds — if the component lists sources explicitly rather than globbing, add `command_parse.c` to that `CMakeLists.txt` `SRCS`).

- [ ] **Step 6: Host tests still pass (regression)**

Run: `cd tools/cfg-harness && make test`
Expected: all pass (Task 1's test unaffected).

- [ ] **Step 7: Commit**

```bash
git add components/cloud/include/cloud.h components/cloud/cloud.c components/cloud/commands.c main/
git commit -m "feat(cmd): handle trigger/show_qr — display QR + ack (settles credit)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: HIL — end-to-end Spec A + B on hardware

**Files:** none (verification only).

This is the first full-loop run: cloud trigger → device QR → ack → cloud settle.

- [ ] **Step 1: Flash**

`source ~/.espressif/v5.5/esp-idf/export.sh && ESP_IDF_VERSION=5.5 idf.py -p <port> flash` (find the port at `/dev/cu.usbmodem*`). The board provisions/points at prod (`ditto-admin-brown.vercel.app`).

- [ ] **Step 2: Seed credits + fire a trigger (cloud side)**

For the board's org: grant a credit (admin grant or a DB insert into `credit_balance`), and create an API key with the `devices:trigger` scope. Then:
```bash
curl -s -X POST "https://ditto-admin-brown.vercel.app/api/v1/devices/<board-deviceId>/trigger" \
  -H "Authorization: Bearer <scoped key>" -H "Idempotency-Key: hil-1" \
  -H "Content-Type: application/json" -d '{"action":"show_qr","payload":{"url":"https://maratus.co/hil"}}'
```
Expected: `202 {id, status:"queued"}`.

- [ ] **Step 3: Observe the device**

`python3 tools/read-console.py 30` and watch: the command arrives (`command ... type=trigger`), the device shows the QR of `https://maratus.co/hil`, the countdown ticks and auto-returns to idle, and an ack POST is sent. Visually confirm the QR appears on screen.

- [ ] **Step 4: Confirm the credit settled (cloud side)**

Query the board's org: `credit_ledger` should have a `settle` row for that command carrying the device id, `credit_balance.held` back to 0, and `/api/v1/usage` `by_device` shows the spend under the board's device id. (This proves ack→settle closed the loop.)

- [ ] **Step 5: Negative path**

Fire a trigger with a bad payload (e.g. `{"action":"show_qr","payload":{}}`) → device should NOT change screen and should ack `ok=false`; confirm the cloud `released` the hold (no charge). (Note: the cloud validates `payload.url` at the API and returns 422 for an outright-missing url, so to exercise the device-side false-ack, target a case the API accepts but the device rejects, or temporarily relax — otherwise this step just confirms the API 422 and is sufficient.)

- [ ] **Step 6: Record outcome**

Note the result in project memory (full Spec A+B loop verified on hardware) per the project's memory conventions.

---

## Self-Review notes

- **Spec coverage:** parse action/url (Task 1); trigger dispatch + ack true/false (Task 2); decoupled callback via cloud_set_trigger_cb (Task 2); reuse DEV_QR + countdown (Task 2 app cb, no new code); host test (Task 1); HIL incl settle confirmation + negative path (Task 3).
- **Type consistency:** `command_trigger_show_qr_url(const cJSON*) → const char*`, `cloud_set_trigger_cb`/`cloud_trigger_handler` with `void(*)(const char*)`, used consistently across tasks.
- **Decoupling preserved:** commands.c calls `cloud_trigger_handler()` (not app_state directly); the app provides the cb — same as config-changed.
- **Open HIL note:** the cloud API rejects a missing `payload.url` with 422, so the device-side `ack ok=false` path is mainly a defensive guard; Task 3 Step 5 documents this.
