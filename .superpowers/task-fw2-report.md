# Task 2: devcfg — DEV_PINNED state machinery (TDD, host)

## Branch
`feat/pinned-qr` (verified via `git branch --show-current` before any edits and again before
commit; confirmed on top of Task 1's `596099b`).

## TDD evidence

### RED
Added to `tools/cfg-harness/test_cfg.c`:
- `test_map()`: `assert(screen_for_state(DEV_PINNED) == SCREEN_QR);`
- new `test_home_state()`: `home_state(true) == DEV_PINNED`, `home_state(false) == DEV_IDLE`
- `test_should_sleep()`: `DEV_PINNED` + elapsed → true; `DEV_PINNED` + not-elapsed → false
- wired `test_home_state()` into `main()`

Ran `make` before touching `dev_state.h`/`render_geom.c`/`render_geom.h`/`sleep_policy.c`:

```
test_cfg.c:130:12: error: call to undeclared function 'home_state'; ISO C99 and later do not support implicit function declarations
test_cfg.c:130:33: error: use of undeclared identifier 'DEV_PINNED'; did you mean 'DEV_PAUSED'?
test_cfg.c:466:45: error: use of undeclared identifier 'DEV_PINNED'; did you mean 'DEV_PAUSED'?
test_cfg.c:467:45: error: use of undeclared identifier 'DEV_PINNED'; did you mean 'DEV_PAUSED'?
5 errors generated.
make: *** [run] Error 1
```
Compile-error RED (missing symbols), as the plan anticipates.

### GREEN
- `dev_state.h`: appended `DEV_PINNED` at the **end** of `dev_state_t` (after `DEV_PAUSED`) —
  no renumbering of existing states.
- `render_geom.c`: `case DEV_PINNED: return SCREEN_QR;` added to `screen_for_state`'s switch
  (placed next to `DEV_QR` for readability); new pure fn
  `dev_state_t home_state(bool pin_set) { return pin_set ? DEV_PINNED : DEV_IDLE; }`.
- `render_geom.h`: declared `dev_state_t home_state(bool pin_set);` next to `screen_for_state`
  (no new include needed — `bool` already reaches this header transitively via
  `device_config.h` → `<stdbool.h>`).
- `sleep_policy.c`: `should_sleep` state guard changed from `if (state != DEV_IDLE) return false;`
  to `if (state != DEV_IDLE && state != DEV_PINNED) return false;`.

```
$ cd tools/cfg-harness && make clean && make && ./run
...
test_map OK
test_home_state OK
...
test_should_sleep OK
...
ALL TESTS PASSED
```
All 25 test functions pass (24 pre-existing + `test_home_state`), zero compiler warnings
(`-Wall -Wextra`, plain `cc`).

## Switch-exhaustiveness sweep
Grepped the whole tree (`components/`, `main/`, excluding `build/`) for every `dev_state_t`
switch/case site: `screen_for_state` in `components/devcfg/render_geom.c` is the **only**
`switch (dev_state_t)` in the codebase. No other exhaustiveness sites exist — `ui.c`,
`app_state.c`, `app_main.c` all branch on specific states via `if`/`==` comparisons (e.g.
`st == DEV_PROCESSING || st == DEV_QR`), which don't require a `DEV_PINNED` arm for this task
(those are Task 3/5's job per the plan). `idf.py build` produced zero warnings (verified via
`grep -i warning` on the full log, 0 matches), confirming no `-Wswitch`/exhaustiveness diagnostic
fired anywhere in the tree.

## Firmware build
IDF 5.5 env sourced via `~/.espressif/v5.5/esp-idf/export.sh`. `idf.py build` from repo root
(ran in background, ~40s):

```
[6/34] Building C object esp-idf/devcfg/CMakeFiles/__idf_devcfg.dir/sleep_policy.c.obj
[7/34] Building C object esp-idf/devcfg/CMakeFiles/__idf_devcfg.dir/render_geom.c.obj
...
[16/34] Linking C static library esp-idf/devcfg/libdevcfg.a
...
[31/34] Linking CXX executable ditto-firmware.elf
[32/34] Generating binary image from built executable
Successfully created esp32p4 image.
Generated /Users/eren/Projects/ditto-firmware/build/ditto-firmware.bin
ditto-firmware.bin binary size 0x195a60 bytes. Smallest app partition is 0x200000 bytes. 0x6a5a0 bytes (21%) free.

Project build complete.
```
`grep -ci warning` on the full build log → `0`. Clean, no new warnings.

## Files touched (exactly the plan's set, plus the test file)
- `components/devcfg/include/dev_state.h` — `DEV_PINNED` appended at the end.
- `components/devcfg/render_geom.c` — `screen_for_state` case + new `home_state()`.
- `components/devcfg/include/render_geom.h` — `home_state()` declaration.
- `components/devcfg/sleep_policy.c` — `should_sleep` state guard widened to include `DEV_PINNED`.
- `tools/cfg-harness/test_cfg.c` — `test_map` assertion, new `test_home_state()`,
  `test_should_sleep` assertions, wired into `main()`.

(`components/devcfg/include/sleep_policy.h` was read but not modified — its signature/doc
comment already covers this correctly with no change needed; the plan listed it as "modify"
alongside the .c but the header needed no edit.)

## Self-review
- Enum append is at the true end (after `DEV_PAUSED`), preserving every existing numeric value
  — anything that (de)serializes `dev_state_t` as a raw int (none found; states are always
  compared/switched symbolically) stays compatible.
- `home_state` is pure (no I/O, no statics) as required — lives in `render_geom.c` alongside
  `screen_for_state`, its natural sibling.
- `should_sleep`'s new condition is the minimal widening the plan asked for; existing
  `DEV_QR`/`DEV_IDLE` cases in `test_should_sleep` are unchanged and still pass.
- Confirmed via grep that `screen_for_state` is the sole `switch(dev_state_t)` site — Tasks
  3 (`ui.c`) and 5 (`app_state.c`) are explicitly where `DEV_PINNED` gets wired into rendering
  and go-home call sites; this task only had to make the state *representable* and
  *sleep-compatible*, which is done.
- Did not touch `ui.c`, `commands.c`, `app_state.c`, `sdkconfig`, or `BUILD.md` — those are
  Tasks 3-6, out of scope here.

## Concerns
None. Both gates (host suite, firmware build) are clean; scope stayed to the plan's files
(plus the untouched header noted above, which needed no edit).
