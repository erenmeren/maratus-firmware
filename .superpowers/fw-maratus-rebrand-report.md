# Maratus rebrand — maratus-firmware

Branch: `fw/maratus-rebrand` (off `main` @ cd53aa3). Not pushed.

## 1. User-visible strings

| File:line | Before | After |
|---|---|---|
| `components/ui/ui.c:63` (BOOT_STAGES `BOOT_CLOUD`) | `"Contacting Maratus..."` | `"Contacting Maratus..."` |
| `components/ui/include/ui.h:33` (matching comment) | `// 70%  "Contacting Maratus..."` | `// 70%  "Contacting Maratus..."` |
| `components/ui/ui.c:752` (`build_splash` title) | `lv_label_set_text(title, "Maratus");` | `"Maratus"` (still 64px bold, centered — unchanged font/layout calls) |
| `components/ui/ui.c:726` (SETUP_STEPS[0]) | `"1.  Open your Maratus dashboard"` | `"1.  Open your Maratus dashboard"` (double-space preserved) |
| `components/ui/ui.c:249` (QR fallback URL) | `"https://maratus.co"` | `"https://maratus.co"` |
| `components/ui/ui_settings.c:14` (comment) | `// VISUAL: restyled to match the Maratus staff mockups...` | `...the Maratus staff mockups...` |
| `main/Kconfig.projbuild:1` | `menu "Maratus firmware (dev)"` | `menu "Maratus firmware (dev)"` |
| `main/Kconfig.projbuild:16` | `string "Maratus Cloud base URL"` | `string "Maratus Cloud base URL"` |
| `main/Kconfig.projbuild:19` (help text) | `Base URL of the Maratus Cloud deployment...` | `Base URL of the Maratus Cloud deployment...` |

### Extra site found in the sweep, fixed beyond the enumerated list
- `main/Kconfig.projbuild:17` — the `MARATUS_API_BASE_URL` prompt's placeholder
  **default** value `"https://your-maratus-deployment.vercel.app"` also said
  "maratus" (not literally called out in the brief, but sitting right next to
  the prompt/help text that *was*, and left stale it would read as a miss).
  Changed to `"https://your-maratus-deployment.vercel.app"`.

### Sweep results — nothing else user-visible
Grepped the whole repo (excluding `build/`, `managed_components/`) for
`"..."` strings containing `maratus`/`Maratus`, case-insensitive, plus a targeted
read of `ui_wifi.c`, `ui_settings.c`'s About/Settings screens, and
`main/app_state.c`'s `build_about_info()` (info_block builder). No further
user-visible occurrences exist. Everything else that still says "maratus" is
one of:
- **KEEPS** you named explicitly: NVS namespace `"maratus"` (`main/app_state.c`,
  `components/cloud/cloud.c:207`, `components/appcfg/appcfg.c:9`
  `MARATUS_NS`), component `mqtt_maratus` + header `mqtt_client_maratus.h`, task
  names `maratus_poll`/`maratus_provision`, log TAG `"maratus"`
  (`main/app_main.c:20`), `CONFIG_MARATUS_*` Kconfig symbol names,
  `project(maratus-firmware)` / `maratus-firmware.bin`.
- Embedded font asset names (`fonts/maratus_sans.ttf`, `fonts/maratus_sans_bold.ttf`
  and their derived `_binary_maratus_sans_*` symbols in `components/assets/`) —
  internal EMBED_FILES symbol names, not shown to a user, left untouched
  (renaming would mean renaming the physical font files too — out of scope).
- `CMakeLists.txt:16` `REGEX "PATCH \(Maratus\) lodepng PSRAM"` — a build-time
  guard that greps a patch marker comment inside gitignored
  `managed_components/`. Purely internal build tooling; left untouched.
- `ESP_LOGI(TAG, "Maratus firmware boot (M2 skeleton)")` in `main/app_main.c:79`
  — serial-console debug log only, never reaches the device screen. I
  classified this the same as the other internal/invisible items and left it
  alone; flagging here in case you'd rather it read "Maratus" for grep-ability
  during bring-up.
- Doc comments referencing the **other repo** `maratus-admin` (e.g.
  `components/ui/include/ui.h:9-10`, `components/devcfg/include/device_config.h:8,14`,
  `components/devcfg/qr_style.c`/`.h` "CLOUD PARITY" comments,
  `tools/cfg-harness/test_cfg.c:567`) — these correctly name the actual other
  repository, which this task did not ask me to rename. Left alone.

## 2. Brand colors

### `components/ui/ui.c`
- Line 22-25: replaced
  ```c
  #define MARATUS_GREEN   0x10A765
  #define MARATUS_ONLINE  0x37C871
  #define MARATUS_GREY    0x888888
  ```
  with
  ```c
  #define MARATUS_AMBER 0xE8A33D
  #define MARATUS_INK   0x111827
  #define STATUS_ONLINE 0x37C871
  #define STATUS_GREY   0x888888
  ```
  (`MARATUS_ONLINE`/`MARATUS_GREY` renamed to `STATUS_ONLINE`/`STATUS_GREY` as
  explicitly invited — values unchanged, both call sites at line 150 and 938
  updated to match.)
- `build_splash()` (~line 746-782): bg `MARATUS_GREEN`→`MARATUS_AMBER`; title
  text white→`MARATUS_INK` (font/size/centering untouched); progress-bar
  track (`LV_PART_MAIN`, `LV_OPA_30`) white→`MARATUS_INK`; progress-bar
  indicator (`LV_PART_INDICATOR`, solid) white→`MARATUS_INK`; boot status
  label white@`LV_OPA_80`→`MARATUS_INK`@`LV_OPA_80`.

### Pre-claim/provisioning screen — default-color finding
The screen at the old `ui.c:748` (`build_splash`) is only the *generic*
splash/fallback (shown when `s_cfg` is `NULL`/invalid) — it is **not** what
actually renders the SETUP_STEPS + pairing-code screen. Tracing
`main/app_main.c:148-161`, an unclaimed device calls
`provisioning_default_setup_config()` (`components/devcfg/provisioning.c`) to
build a real, populated `device_config_t` with the SETUP screen's objects
(title, `OBJ_STEPS`, `OBJ_PAIRING_CODE`), then `ui_set_config()` +
`ui_render_state(DEV_SETUP)` sends it through `build_screen()`, which reads
`brand_bg`/`brand_fg`/`brand_accent` off that struct (`ui.c:789-791`) — *not*
the `MARATUS_GREEN` `#define` directly. So the real defaults for this screen
live in `provisioning_default_setup_config()`:
```c
// before
cfg->brand_bg = 0x0B5D3B;      // dark green (distinct from MARATUS_GREEN)
cfg->brand_fg = 0xFFFFFF;      // white
cfg->brand_accent = 0x10A765;  // MARATUS_GREEN
// after
cfg->brand_bg = 0xE8A33D;      // Maratus amber
cfg->brand_fg = 0x111827;      // Maratus ink
cfg->brand_accent = 0x111827;  // Maratus ink
```
`brand_fg` (white) drives the title/steps/pairing-code text and the
always-drawn serial-number label (`ui.c:839`) — changed to ink so it stays
legible against amber. `brand_accent` isn't actually consumed by any object
on `SCREEN_SETUP` today (no spinner/countdown there), but I converted it too
for consistency with the rest of the palette and because this struct's
colors apply as the global fallback for *any* screen number if ever
reused — ink keeps it readable against the new amber bg rather than
reproducing the old green.

### Repo-wide sweep for `0x10A765`
Also found (and converted) two more fallback uses in
`components/devcfg/cfg_parse.c` (`cfg_parse_json`, used when the cloud's JSON
omits a brand-color field):
```c
// before
cfg->brand_bg     = parse_hex_color(..., 0x10A765);
cfg->brand_fg     = parse_hex_color(..., 0xFFFFFF);
cfg->brand_accent = parse_hex_color(..., 0x10A765);
// after
cfg->brand_bg     = parse_hex_color(..., 0xE8A33D);  // Maratus amber
cfg->brand_fg     = parse_hex_color(..., 0x111827);  // Maratus ink
cfg->brand_accent = parse_hex_color(..., 0x111827);  // Maratus ink
```
`brand_fg`'s fallback (`0xFFFFFF`, not literally `0x10A765`) was changed too,
same reasoning as above: leaving it white while `brand_bg`'s fallback became
amber would reproduce the exact "white-on-amber is low contrast" problem
the brief called out, just one call-site over. Flagging this as a deliberate
addition beyond the literal `0x10A765` grep, in case you'd rather keep white
here (this path only fires when the cloud sends a config missing these
fields entirely — in practice tenant branding always sets them, so it's a
defensive default, not something a real customer will see).

No other `0x10A765`/`MARATUS_GREEN` occurrences remain in source. The four
remaining hits are all in `tools/cfg-harness/test_cfg.c` — arbitrary hex
values baked into hand-written JSON test payloads/assertions that exercise
the *parser*, not brand identity (confirmed against
`tools/cfg-harness/fixtures/sample-config.json`, which sets its own explicit
colors). That harness is a standalone host-side `Makefile` build (`cc` +
vendored cJSON), not part of `idf.py build`, and out of this task's build
gate — left untouched, still green either way since none of these
assertions exercise the fallback-default path I changed.

### `components/ui/ui_wifi.c`
- Line 22: replaced `#define MARATUS_GREEN 0x10A765` with
  ```c
  #define MARATUS_AMBER 0xE8A33D
  #define MARATUS_INK   0x111827
  #define MARATUS_RUST  0xB4541F   // accent text on a white/light panel
  ```
- Line ~123: screen bg `MARATUS_GREEN`→`MARATUS_AMBER`.
- `s_connect_lbl` (~line 244): its parent (`btn`, the Connect/Next pill) has
  an **opaque white** background (`lv_color_white()` + `LV_OPA_COVER`) — a
  true white panel — so per the brief its text goes green→**`MARATUS_RUST`**
  (`0xB4541F`), not ink.
- Other white-on-green text found on this screen, all sitting directly on
  the amber screen background (or a translucent ~10-40% white overlay over
  it, which still reads as amber-dominant) — converted white→`MARATUS_INK`:
  - `title` "Choose your Wi-Fi" (~136)
  - `s_list_sub` status line (~142)
  - `exit_ic` (X chip icon, on a 20-40%-opacity white circle over amber) (~162)
  - `back_ic` (back chip icon, same treatment) (~195)
  - `s_pw_title` selected-network name (~202)
  - `s_pw_status` password-stage status line (~211)
  - `add_row()` list-row icon + label (~74, ~80) — each row's "card" is only
    `LV_OPA_10` white over the amber screen, i.e. still amber-dominant, not a
    real light panel, so its icon/text also went white→ink for contrast.
  Left as white (unchanged): the row-card and chip **backgrounds** themselves
  (`lv_color_white()` at `LV_OPA_10/20/40`, lines 64/156/190) and the Connect
  button's own opaque white background (238) — those are panel fills, not
  text, and stay white by design.

No other `0x10A765` uses exist outside `ui.c`, `ui_wifi.c`,
`provisioning.c`, and `cfg_parse.c` (confirmed via repo-wide grep).

### Untouched, as instructed (semantic, not brand)
`STATUS_ONLINE`/`STATUS_GREY` (`0x37C871`/`0x888888`, `ui.c`) and
`ABOUT_DOT_GREEN`/`ABOUT_DOT_RED` (`0x2BB673`/`0xE0564B`,
`components/ui/ui_settings.c:479-480`) — values unchanged.

## 3. Version + URL config

- `main/Kconfig.projbuild:30` — `config MARATUS_FW_VERSION` default
  `"0.18.3"` → `"0.19.0"`.
- `sdkconfig` (gitignored, not tracked by git — see note below) —
  `CONFIG_MARATUS_FW_VERSION="0.18.3"` → `"0.19.0"`;
  `CONFIG_MARATUS_API_BASE_URL="https://ditto-admin-brown.vercel.app"` →
  `"https://api.maratus.co"`. Diffed the file before/after `idf.py build`
  (saved a pre-build copy first): the **only** other change is the
  auto-regenerated menu comment header (`# Maratus firmware (dev)` →
  `# Maratus firmware (dev)`, a direct/correct consequence of the
  `Kconfig.projbuild` menu-name rename, not stray churn). Wi-Fi SSID/password
  and the dev device key are byte-identical.

## 4. Deliberate KEEPS — confirmed untouched
- `CONFIG_MARATUS_*` Kconfig symbol names (only prompts/defaults/help text
  changed).
- NVS namespace `"maratus"` — `main/app_state.c` (2 call sites + the
  block comment), `components/cloud/cloud.c:207` (`CFG_NVS_NS`),
  `components/appcfg/appcfg.c:9` (`MARATUS_NS`). CRITICAL keep, confirmed
  untouched.
- `project(maratus-firmware)` in `CMakeLists.txt` and the `maratus-firmware.bin`
  artifact name.
- Component `mqtt_maratus`, header `mqtt_client_maratus.h`, task names
  `maratus_poll` / `maratus_provision`, log TAG `"maratus"`, MQTT topics
  (`d/%s/cmd`, `d/%s/ack`, `d/%s/hb`, `d/%s/cfg/get`).

I was not tempted to rename any of these — no edits touched them.

## Build

```
cd /home/meren/projects/maratus-firmware
. ~/.espressif/v5.5/esp-idf/export.sh
idf.py build
```
Succeeded, 1862/1862 build steps, no warnings from any touched file.

```
Generated /home/meren/projects/maratus-firmware/build/maratus-firmware.bin
maratus-firmware.bin binary size 0x195ef0 bytes. Smallest app partition is 0x200000 bytes. 0x6a110 bytes (21%) free.
```
`maratus-firmware.bin` = 0x195ef0 bytes = **1,662,704 bytes (~1.59 MiB)**,
confirmed present at `build/maratus-firmware.bin` (`ls -la` also shows
1662704 bytes).

## Self-review notes
- `git status` after the commit is clean relative to the repo's own baseline:
  only the 7 tracked source files are modified (`cfg_parse.c`,
  `provisioning.c`, `ui.h`, `ui.c`, `ui_settings.c`, `ui_wifi.c`,
  `Kconfig.projbuild`). `build/` and `managed_components/` are gitignored and
  were not added. `sdkconfig` is **not tracked by git at all** in this repo
  (`git ls-files` confirms it's absent, `.gitignore` lists it) — my two
  required edits there are real and necessary for the build to actually
  target `api.maratus.co` / report `0.19.0`, but they don't appear in `git
  status`/the commit; that's expected given the repo's existing setup, not
  an oversight.
- `.superpowers/` was already untracked before I started (pre-existing
  reports from earlier tasks); I added this report file there without `git
  add`-ing it, matching that existing convention.
- Two color decisions extend past the literally-enumerated sites (provisioning
  default `brand_fg`/`brand_accent`, and `cfg_parse.c`'s `brand_fg` fallback)
  — flagged above with reasoning in case you'd rather they stayed literal.
- One extra Kconfig site fixed (the `your-maratus-deployment.vercel.app`
  placeholder default) — flagged above.
- One site deliberately left as-is despite containing "Maratus": the
  `ESP_LOGI` boot line in `app_main.c` — serial/debug-only, never reaches
  the device screen, treated like the other internal/invisible keeps.
  **Overturned in the polish pass below** — the product mandate is "no
  Maratus anywhere," so dev-facing text counts too.

## Polish pass (review follow-up, second commit)

Coordinator review verdict: Approved, zero Critical/Important, three Minor
items. All three addressed on top of `04a8990`, in a second commit
`fd43a06` on the same branch.

1. `components/devcfg/cfg_parse.c:183-184` — added the requested comment
   directly above the `brand_fg` fallback line, documenting the cross-repo
   invariant it depends on:
   ```c
   // Fallback is effectively dead today: maratus-admin's resolveBrandTokens (lib/color.ts)
   // always emits explicit 6-hex brand colors in every config push. Keep it contract-safe
   // if that ever changes.
   cfg->brand_fg = parse_hex_color(..., "brandFg")..., 0x111827);
   ```
2. `components/ui/include/ui.h:9` — `"Maratus on-screen UI: a generic
   renderer..."` → `"Maratus on-screen UI: ..."`. Line 10's `"Mirrors
   PRINTER_SCREENS in maratus-admin"` left untouched, as instructed — that's
   the real, still-current name of the other repo.
3. `main/app_main.c:79` — boot log `ESP_LOGI(TAG, "Maratus firmware boot (M2
   skeleton)")` → `ESP_LOGI(TAG, "Maratus firmware boot")`. Matched the
   coordinator's literal target string, which dropped the "(M2 skeleton)"
   suffix — flagging in case that drop wasn't intended and the suffix
   should have been kept (e.g. `"Maratus firmware boot (M2 skeleton)"`).

### Rebuild
Incremental `idf.py build` after the three edits: succeeded (22/22 steps —
only the touched translation units + link/image-gen steps re-ran).
```
Generated /home/meren/projects/maratus-firmware/build/maratus-firmware.bin
maratus-firmware.bin binary size 0x195ee0 bytes. Smallest app partition is 0x200000 bytes. 0x6a120 bytes (21%) free.
```
New size: **0x195ee0 = 1,662,688 bytes**, 16 bytes smaller than the first
commit's 1,662,704 bytes (net effect of dropping "(M2 skeleton)" from the
boot string outweighing "Maratus"→"Maratus" being one character longer in
three other spots, plus the new comment which is compiled out).

### Self-review
- `git status` clean relative to baseline after the second commit (only
  `.superpowers/` untracked, same as before either commit).
- Second commit touches exactly the 3 files named in the review, nothing
  else — confirmed via `git diff --cached` before committing.
- Did not amend the first commit; this is a distinct second commit as
  instructed.
