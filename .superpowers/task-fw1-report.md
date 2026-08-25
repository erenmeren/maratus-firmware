# Task 1: devcfg — pin config block (TDD, host)

## Branch
`feat/pinned-qr` (verified via `git branch --show-current` before any edits and again before commit).

## TDD evidence

### RED
Added `test_pin_block()` to `tools/cfg-harness/test_cfg.c` (cases a-e) plus a `pin_set==false`
assertion to the existing `test_parse()` (case f, using the unmodified fixture which has no
`pin` key). Wired `test_pin_block()` into `main()`. Ran `make` before touching
`device_config.h`/`cfg_parse.c`:

```
test_cfg.c:83:16: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:608:15: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:609:22: error: no member named 'pin_url' in 'device_config_t'
test_cfg.c:614:15: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:619:15: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:624:15: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:632:15: error: no member named 'pin_set' in 'device_config_t'
test_cfg.c:633:22: error: no member named 'pin_url' in 'device_config_t'
test_cfg.c:634:15: error: no member named 'pin_url' in 'device_config_t'
9 errors generated.
make: *** [run] Error 1
```
Compile-error RED, as the plan says is acceptable evidence.

### GREEN
Added `bool pin_set; char pin_url[512];` to `device_config_t` (sibling of `mqtt{}`), and a
parse block in `cfg_parse_json` right after the `mqtt` block, using the file's own `copy_str()`
helper (not the plan's raw `strncpy` snippet — `cfg_parse.c` already has `copy_str(dst, cap, src)`
used by every other char[] field, so I matched that idiom instead, per the task instructions).

```
$ cd tools/cfg-harness && make && ./run
...
test_parse OK
...
test_pin_block OK
test_mqtt_block OK
...
ALL TESTS PASSED
```
All 24 test functions pass, zero compiler warnings during `make` (plain `-Wall -Wextra`, no
new diagnostics).

## Test matrix covered (test_pin_block, plus case f in test_parse)
- (a) `{"pin":{"url":"https://x/menu"}}` → `pin_set==true`, url copied verbatim.
- (b) `"pin":null` → `pin_set==false`.
- (c) `pin` absent → `pin_set==false`.
- (d) `{"pin":{"url":""}}` (empty string) → `pin_set==false`.
- (e) 600-char URL → truncated to 511 chars + NUL (buffer is `char[512]`), `pin_set==true`.
- (f) existing full fixture (`fixtures/sample-config.json`, no `pin` key) → `pin_set==false`,
  i.e. byte-identical-behavior-with-old-cloud assertion, added inline to `test_parse()`.

## Firmware build
IDF 5.5 env sourced via `~/.espressif/v5.5/esp-idf/export.sh` (present and working — no
`esp_wifi_remote` breakage, no need for `tools/patch-deps.sh`). `idf.py build` from repo root:

```
...
[12/33] Building C object esp-idf/devcfg/CMakeFiles/__idf_devcfg.dir/cfg_parse.c.obj
[14/33] Linking C static library esp-idf/devcfg/libdevcfg.a
...
[31/33] Generating binary image from built executable
Successfully created esp32p4 image.
Generated /Users/eren/Projects/ditto-firmware/build/ditto-firmware.bin
ditto-firmware.bin binary size 0x195a60 bytes. Smallest app partition is 0x200000 bytes. 0x6a5a0 bytes (21%) free.

Project build complete.
```
Re-ran `idf.py build` piped through `grep -i warning` afterward — zero matches. Clean, no new
warnings.

## Files touched (exactly the three from the plan)
- `components/devcfg/include/device_config.h` — `+bool pin_set; +char pin_url[512];` sibling
  of `mqtt{}`, with the plan's exact doc comment.
- `components/devcfg/cfg_parse.c` — new `pin` object block after the `mqtt` block, using
  `copy_str()` (existing helper) instead of raw `strncpy`.
- `tools/cfg-harness/test_cfg.c` — `test_pin_block()` (cases a-e) + one assertion added to
  `test_parse()` (case f) + wired into `main()`.

## Commit
`596099b` — `feat(pin): parse config pin block into device_config_t` (matches plan's exact
message). Staged explicitly via `git add <3 files>` — did not touch the untracked
`.superpowers/` directory that was present before this task started.

## Self-review
- Field placement matches the plan (sibling of `device{}`/`mqtt{}`, before `screens[]`).
- Truncation is safe: `copy_str` does `strncpy(dst, src, cap-1); dst[cap-1]='\0'` — same
  guarantee as the plan's raw strncpy snippet, but reuses the file's existing idiom (task
  explicitly asked to check for and prefer a helper if one exists — it does, and every other
  char[] field in the file already uses it).
- `pin.url == ""` correctly stays `pin_set=false` (guarded by `purl[0] != '\0'`), matching
  case (d) and the spec's "pin.url non-empty string → pin_set=true" wording.
- `cfg` is `memset(cfg, 0, sizeof(*cfg))` at the top of `cfg_parse_json`, so `pin_set` defaults
  false and `pin_url` defaults empty for every case where the `pin` object/url isn't present —
  no separate reset code needed.
- Did not touch `dev_state.h`, `render_geom.c`, `sleep_policy.c`, `ui.c`, `commands.c`,
  `app_state.c`, `sdkconfig`, or `BUILD.md` — those are Tasks 2-6, out of scope here.

## Concerns
None. Both gates (host suite, firmware build) are clean; scope stayed exactly to the plan's
three files.
