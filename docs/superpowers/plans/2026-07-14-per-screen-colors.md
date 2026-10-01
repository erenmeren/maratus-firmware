# Per-Screen Colors (Firmware) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** The device renders each screen with its per-screen palette override (`config.screens[s].colors`) when present, falling back to the global brand colors otherwise — matching the admin preview shipped in maratus-admin `7052500`.

**Architecture:** `cfg_screen_t` gains an optional per-screen palette (`has_colors` + 4 colors) parsed defensively from the payload's `screens[s].colors`; `build_screen()` resolves the effective palette once per build and threads it to renderers. No protocol change — the cloud already ships the field (guaranteed lowercase `#rrggbb`, all-or-nothing after cloud normalize; firmware still validates all four before trusting it).

**Tech Stack:** ESP-IDF 5.5 / C11 / cJSON; host unit harness `tools/cfg-harness` (`make test`, plain cc).

**Cloud contract:** maratus-admin `docs/superpowers/specs/2026-07-14-per-screen-theming-design.md` §Device contract.

## Global Constraints

- Branch: `feat/per-screen-colors` off `main` (7ad37d8).
- Payload field: `config.screens.{idle|processing|qr|sent|error|paused|setup}.colors = { accent, bg, fg, muted }`, each `#rrggbb` lowercase. Firmware accepts `#`-optional 6-digit hex; **all four must parse or the override is ignored** (`has_colors = false`) — never partially applied, never crashes on malformed input.
- Absent/malformed `colors` ⇒ identical behavior to today (global `brand_*`).
- The Settings UI, boot splash, and Wi-Fi setup screens keep global colors (they are not config screens).
- Commit style `feat(cfg): …` / `feat(ui): …`, ending `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task F1: Parse per-screen colors into `cfg_screen_t` (host-tested)

**Files:**
- Modify: `components/devcfg/include/device_config.h`
- Modify: `components/devcfg/cfg_parse.c`
- Test: `tools/cfg-harness/test_cfg.c`

**Interfaces:**
- Produces: `cfg_screen_t` fields `bool has_colors; uint32_t col_bg, col_fg, col_accent, col_muted;` — Task F2 reads exactly these names.

- [ ] **Step 1: Write failing host tests**

In `tools/cfg-harness/test_cfg.c`, find how existing cfg_parse tests build payload JSON (there are existing `cfg_parse_json` tests) and add, following the file's existing test-function + assertion idiom, a test function `test_screen_colors(void)` (registered wherever the file runs its tests) covering:

```c
// 1) valid override on idle → has_colors true, values parsed
//    screens.idle = {"objects":[],"colors":{"accent":"#10a765","bg":"#111827","fg":"#f9fafb","muted":"#9ca3af"}}
//    assert cfg.screens[SCREEN_IDLE].has_colors == true
//    assert cfg.screens[SCREEN_IDLE].col_bg == 0x111827 && col_fg == 0xF9FAFB
//    && col_accent == 0x10A765 && col_muted == 0x9CA3AF
// 2) other screens in the same payload (no colors key) → has_colors false
// 3) partial override (only 3 of 4 keys) → has_colors false
// 4) invalid hex in one key ("nope") → has_colors false
// 5) colors present but not an object ("colors":"dark") → has_colors false
// 6) '#'-less hex ("10a765") → accepted (has_colors true)
```

Run: `cd tools/cfg-harness && make test` — must FAIL to compile (fields don't exist) or fail assertions.

- [ ] **Step 2: Add the fields**

In `device_config.h`:

```c
typedef struct {
    cfg_object_t objects[CFG_MAX_OBJECTS];
    int n;
    // Per-screen palette override (config.screens[s].colors). All four parse or
    // has_colors stays false — never partially applied.
    bool     has_colors;
    uint32_t col_bg, col_fg, col_accent, col_muted;
} cfg_screen_t;
```

- [ ] **Step 3: Parse in `cfg_parse.c`**

Add a strict variant next to `parse_hex_color` (which cannot distinguish invalid from default):

```c
/* Strict: true only if s is 6 hex digits (optionally '#'-prefixed); no default. */
static bool parse_hex_color_strict(const char *s, uint32_t *out) {
    if (!s) return false;
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i]; int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}
```

In the screens loop in `cfg_parse_json` (after `parse_screen(objs, &cfg->screens[i]);`):

```c
        cJSON *cols = scr ? cJSON_GetObjectItem(scr, "colors") : NULL;
        cfg_screen_t *cs = &cfg->screens[i];
        cs->has_colors =
            cJSON_IsObject(cols) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "bg")),     &cs->col_bg) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "fg")),     &cs->col_fg) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "accent")), &cs->col_accent) &&
            parse_hex_color_strict(cJSON_GetStringValue(cJSON_GetObjectItem(cols, "muted")),  &cs->col_muted);
```

(`memset(cfg, 0, …)` at the top of `cfg_parse_json` already zeroes `has_colors` for the absent case; short-circuit partial writes to `col_*` are harmless because `has_colors` ends false.)

- [ ] **Step 4: Green + commit**

Run: `cd tools/cfg-harness && make test` — ALL tests pass (existing + new).

```bash
git add components/devcfg/include/device_config.h components/devcfg/cfg_parse.c tools/cfg-harness/test_cfg.c
git commit -m "feat(cfg): parse per-screen palette override (screens[s].colors, all-or-nothing)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task F2: `build_screen()` renders with the effective palette

**Files:**
- Modify: `components/ui/ui.c`

**Interfaces:**
- Consumes: `cfg_screen_t.has_colors/col_bg/col_fg/col_accent/col_muted` from Task F1.

- [ ] **Step 1: Resolve the effective palette once per build**

In `build_screen()` (ui.c:515), after `const cfg_screen_t *sc = &s_cfg->screens[screen];` add:

```c
    // Effective palette: the screen's override when present, else global brand.
    const uint32_t bg     = sc->has_colors ? sc->col_bg     : s_cfg->brand_bg;
    const uint32_t fg     = sc->has_colors ? sc->col_fg     : s_cfg->brand_fg;
    const uint32_t accent = sc->has_colors ? sc->col_accent : s_cfg->brand_accent;
```

Then, WITHIN `build_screen()` only, replace every `s_cfg->brand_bg` with `bg`, `s_cfg->brand_fg` with `fg`, `s_cfg->brand_accent` with `accent` — that is: the screen bg (line ~521), the render dispatch cases (text/icon-placeholder/clock/wifi/spinner/countdown/pairingCode/steps/default, lines ~534-548), and the setup-screen serial label (line ~567).

- [ ] **Step 2: Thread fg into `render_image` placeholders**

`render_image()` internally uses `s_cfg->brand_fg` for its not-yet-cached placeholder (ui.c ~197-208). Add a `uint32_t fg` parameter to `render_image` (and update its call sites inside `build_screen`: OBJ_IMAGE and the OBJ_ICON upload branch) so image/icon placeholders also honor the screen palette. `render_wordmark` (OBJ_LOGO) stays untouched — the logo object type is retired cloud-side; it's a dead legacy path.

- [ ] **Step 3: Gates**

- Host harness still green: `cd tools/cfg-harness && make test`.
- Full firmware build: `. ~/.espressif/v5.5/esp-idf/export.sh` then `idf.py build` from the repo root (if it fails inside `esp_wifi_remote`, run `./tools/patch-deps.sh` and rebuild; NEVER set `ESP_IDF_VERSION` to a patch version like 5.5.4 — see BUILD.md).
- Expected: build succeeds (`maratus-firmware.bin` produced).

- [ ] **Step 4: Commit**

```bash
git add components/ui/ui.c
git commit -m "feat(ui): build_screen renders with the per-screen palette override when present

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task F3: Merge + report (HIL deferred)

- [ ] Merge `feat/per-screen-colors` into `main` (fast-forward or merge commit per repo habit), push both.
- [ ] Report: HIL verification (flash desk device b580, set an override in the admin, confirm the screen changes on next config poll) is deferred to the owner — list the exact HIL steps in the report.
