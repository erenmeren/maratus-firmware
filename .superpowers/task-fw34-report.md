# Tasks 3 & 4 — Pinned QR Firmware

Branch: `feat/pinned-qr` (verified before starting). Both tasks implemented as two commits, in plan order.

## Task 3: ui — suppress countdown when rendering pinned

File: `components/ui/ui.c`

Changes:
- Added `static dev_state_t s_render_state = DEV_BOOT;` next to `s_qr_url` (line 27), with a comment noting it's the last state passed to `ui_render_state` and that `OBJ_COUNTDOWN` reads it.
- In `ui_render_state`, set `s_render_state = st;` as the first statement inside the `lvgl_port_lock(0)` critical section, before any of the existing per-render resets and before `build_screen` runs.
- In `build_screen`'s object switch: `case OBJ_COUNTDOWN: if (s_render_state != DEV_PINNED) render_countdown(scr, o, fg, accent); break;` — call args unchanged.

Grep check per plan Step 3: `s_render_state` appears at 3 sites — the declaration, the single write in `ui_render_state`, and the read in the `OBJ_COUNTDOWN` case. `ui_render_state` is the only writer. Confirmed.

Commit: `baf3fb5` — `feat(pin): pinned render suppresses countdown widget`

Build tail (clean, no warnings):
```
[6/12] Building C object esp-idf/ui/CMakeFiles/__idf_ui.dir/ui.c.obj
[7/12] Linking C static library esp-idf/ui/libui.a
...
Project build complete.
```

## Task 4: cloud — pin command = ack + config refresh

File: `components/cloud/commands.c`

Added a `"pin"` branch in `dispatch_command`, immediately after the `"config-changed"`/`"refresh"` branch, mirroring it exactly: invokes the registered `cloud_config_changed_handler()` callback if set, then `ack(id, true)`. Includes the plan's one-line comment:

```c
} else if (strcmp(cmd->type, "pin") == 0) {
    // pin: state lives in config (ETag-folded); refreshing is the whole job.
    void (*pcb)(void) = cloud_config_changed_handler();
    if (pcb) pcb();
    ack(id, true);
} else if (strcmp(cmd->type, "firmware-update") == 0) {
```

Payload is deliberately ignored, matching the spec (config is the sole source of truth).

**Step 2 (host coverage check):** `tools/cfg-harness/Makefile` compiles `command_parse.c` (generic `command_parse_into`, type-agnostic — no per-type branching, so no host-test change needed for a new type string) but does **not** compile `commands.c`/`dispatch_command` (it pulls in FreeRTOS, `esp_log.h`, `ui.h`, `cloud.h` — not host-portable, and isn't in the harness `SRCS` list for any existing command type either). So: **dispatch not host-covered (matches existing types)** — no test extension made, per the plan's fallback instruction.

Commit: `a973bef` — `feat(pin): ack pin command and refresh config`

Build tail (clean, no warnings):
```
[6/12] Building C object esp-idf/cloud/CMakeFiles/__idf_cloud.dir/commands.c.obj
[7/12] Linking C static library esp-idf/cloud/libcloud.a
...
Project build complete.
```

## Self-review

- Task 3: lock ordering matches the plan (state set before `build_screen`, inside `lvgl_port_lock`); no new statics beyond the one specified; existing `render_countdown` call args untouched; only `DEV_PINNED` changes behavior, all other states render the countdown as before.
- Task 4: branch placement and body are a structural mirror of `config-changed`/`refresh` as instructed; used a distinct local var name (`pcb`) to avoid shadowing/redeclaring `cb` from the sibling branch (same scope, `else if` chain) — cosmetic only, behavior identical.
- Both diffs are single-file, minimal, no IDF/sdkconfig changes, no new compiler warnings.

## Concerns

- None blocking. Only soft note: Task 4's `dispatch_command` remains untested at the host level (as is true for every other command type it handles) — coverage gap is pre-existing, not introduced by this change.
