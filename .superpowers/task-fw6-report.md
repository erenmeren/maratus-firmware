# Task fw-6 report — pinned-QR review fix + version/docs/gates

Branch: `feat/pinned-qr` (verified before start).

## Piece A — review fix
Commit `fd47c7d` — `fix(ui): s_qr_url 512B to match pin/trigger url contracts; warn on truncation`
- `components/ui/ui.c`: widened `static char s_qr_url[256]` → `[512]` (matches
  `device_config_t.pin_url` / `dev_command_t.url` 512B contracts).
- Added `static const char *TAG = "ui";` (file had none before) and `#include "esp_log.h"`.
- `ui_set_qr_url`: added `ESP_LOGW(TAG, "qr url len=%d exceeds s_qr_url capacity (%d); truncating", ...)`
  guarded on `strlen(url) > sizeof(s_qr_url) - 1`, before the existing `strncpy`/lock structure
  (unchanged otherwise).

## Piece B — Task 6 (version + docs + gates)
Commit `7d5debc` — `build: bump fw to 0.9.0 + BUILD.md pinned-QR entry`
- `sdkconfig`: `CONFIG_DITTO_FW_VERSION` `0.8.0` → `0.9.0` (file is gitignored; edited on disk,
  **not committed** — see below).
- `main/Kconfig.projbuild`: `DITTO_FW_VERSION` default `0.8.0` → `0.9.0`.
- `BUILD.md`: appended dated entry "## Pinned QR (2026-07-22, pending HIL) — fw 0.9.0" with
  feature summary (persistent `DEV_PINNED` QR state, `s_qr_url` widen + truncation warn) and the
  HIL checklist copied from `docs/superpowers/specs/2026-07-22-pinned-qr-firmware-design.md`
  Testing section (set pin → persistent QR ~2s; trigger → countdown → auto-return to pin; change
  URL → repaint; remove pin → idle; reboot → pin after boot gate; screen-sleep still engages/wakes).

### `sdkconfig` tracked?
**No** — `.gitignore` line 2 lists `sdkconfig`. `git status` never showed it as modified even
after the edit. Only `BUILD.md` and `main/Kconfig.projbuild` were staged/committed.

## Gates (run after Piece A, so the fix is covered)

### `cd tools/cfg-harness && make && ./run`
```
make: `run' is up to date.
test_parse OK
test_geom OK
test_zsort OK
test_map OK
test_home_state OK
test_asset_key OK
test_asset_evict OK
test_clock OK
test_mmss OK
test_qr_timeout OK
test_screen_colors OK
test_device_defaults OK
test_pin_block OK
test_mqtt_block OK
test_mqtt_topics OK
test_provisioning OK
test_wifi_util OK
test_ota_manifest OK
test_should_sleep OK
test_sha256_and_pin OK
test_gesture_swipe_up OK
test_gesture_snap OK
test_devset OK
test_brightness OK
test_trigger_show_qr OK
test_command_parse_into OK
ALL TESTS PASSED
```

### `idf.py build` (from repo root, after `. ~/.espressif/v5.5/esp-idf/export.sh`)
```
[1863/1868] Linking C static library esp-idf/main/libmain.a
[1864/1868] Generating esp-idf/esp_system/ld/sections.ld
[1865/1868] Linking CXX executable ditto-firmware.elf
[1866/1868] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32p4 image...
Merged 3 ELF sections
Successfully created esp32p4 image.
Generated /Users/eren/Projects/ditto-firmware/build/ditto-firmware.bin
[1867/1868] cd .../esptool_py && python check_sizes.py --offset 0xa000 partition ...
ditto-firmware.bin binary size 0x195b30 bytes. Smallest app partition is 0x200000 bytes. 0x6a4d0 bytes (21%) free.

Project build complete.
```
Clean build, exit 0. Checked for warnings: 12 total, all pre-existing and unrelated to this
change — 11 from vendored ESP-IDF FreeRTOS `atomic.h` (`-Wunused-function`) and 1 from the managed
`esp_lvgl_port` component (`on_refresh_done` deprecated). None reference `ui.c`, `app_state.c`,
`app_main.c`, or any file touched by this task — no new warnings introduced.

## Concerns
None. HIL itself is out of scope per the plan ("controller/user-driven — not part of this plan's
tasks") and BUILD.md's new entry is explicitly marked "pending HIL".
