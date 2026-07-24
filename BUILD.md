# Building & flashing ditto-firmware

## Toolchain: ESP-IDF **5.5.x** (required)

The full stack — Waveshare board BSP (`waveshare/esp32_p4_wifi6_touch_lcd_4b`), the
ST7703 panel driver, and esp-hosted/esp_wifi_remote (C6 Wi-Fi over SDIO) — targets
**ESP-IDF 5.5**. 5.5 is what makes the C6 SDIO transport select correctly
(`SLAVE_IDF_TARGET_ESP32C6`); on 5.4.4 networking could not come up. **Do not build
on IDF 6.0** — its `esp_lcd` MIPI-DSI / `json` / `usb` / driver-split changes break
the vendored BSP.

```bash
. ~/.espressif/v5.5/esp-idf/export.sh        # or your 5.5 export.sh
```

All the 5.5-specific config lives in `sdkconfig.defaults` (auto-applied, documented
inline): partition-table offset `0xA000` (the 5.5 bootloader outgrew the old gap),
ESP32-P4 **silicon rev v1.0** unlock (5.5 defaults to v3.1; this board is v1.0), and
the SDIO transport tuning (see "Wi-Fi / C6 SDIO" below).

## Known post-fetch patch (esp_wifi_remote)

After the first `idf.py build` fetches `managed_components/`, apply
`tools/patch-deps.sh` (re-run any time `managed_components/` is re-fetched — it
re-applies the BSP touch-skip + any `WIFI_RMT_*` Kconfig fixups, and no-ops what
isn't needed on 5.5):

```bash
./tools/patch-deps.sh
```

## Build

```bash
idf.py set-target esp32p4
idf.py build            # if it fails in esp_wifi_remote, run ./tools/patch-deps.sh then rebuild
```

> **NEVER set `ESP_IDF_VERSION` to a patch version (e.g. `5.5.4`).** `esp_wifi_remote/Kconfig`
> does `orsource "./idf_v$ESP_IDF_VERSION/Kconfig.slave_select.in"`, which needs the
> **major.minor** form `5.5` to resolve `idf_v5.5/`. With `5.5.4` the path is `idf_v5.5.4/`
> (doesn't exist); `orsource` silently skips it, `SLAVE_IDF_TARGET_ESP32C6` is never defined,
> and the Wi-Fi transport falls back to **SPI** → boot panic
> `spi_mempool_create … "mempool create failed: no mem"` (backlight on, blank screen).
> Let `idf.py` set the version itself, or export exactly `ESP_IDF_VERSION=5.5`. If a stale
> `sdkconfig` already drifted to SPI (`CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE=y`), recover with:
> `./tools/patch-deps.sh && rm sdkconfig && ESP_IDF_VERSION=5.5 idf.py reconfigure` then
> restore the dev creds in `sdkconfig` (`CONFIG_DITTO_API_BASE_URL` / `CONFIG_DITTO_DEVICE_KEY`).
> Verify with `grep CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE sdkconfig`.

## Flash (USB-to-UART port, NOT USB-OTG)

Use the **USB-to-UART** Type-C port. `idf.py -p <port> flash` works (auto-reset). If a
flash ever fails to connect, enter download mode manually: unplug → hold **BOOT** →
replug → release after ~2s; the port re-enumerates to a new `/dev/cu.usbmodem*`
(rediscover with `ls /dev/cu.usbmodem*`).

```bash
idf.py -p <port> flash
```

## Reading the console — use the passive UART reader, NOT `idf.py monitor`

`idf.py monitor` drives this board's DTR/RTS and traps it in download mode. Read the
UART0 console passively instead (holds DTR/RTS low for a normal boot):

```bash
python tools/read-console.py 20        # reads ~20s; opening the port resets the board
```

Opening the serial port resets the device (auto-reset line), so every read starts a
fresh boot — that's expected.

## Wi-Fi / C6 SDIO (the on-board ESP32-C6 radio)

The P4 has no Wi-Fi; it drives the on-board **ESP32-C6** as a Wi-Fi co-processor over
**SDIO** (esp_hosted). Two things matter:

- **C6 slave firmware must match the host** (esp_hosted 2.12.x). Boards ship the C6
  with an old slave (v0.0.6) which works for association but stalls the SDIO data path
  under sustained RF traffic. Update it **once** via host-driven LittleFS slave-OTA
  (build `espressif/esp_hosted:slave` for esp32c6, push it from a host running the
  `host_performs_slave_ota` example — both reuse this board's default SDIO pin map).
- **SDIO mempool stays in INTERNAL DMA RAM** (do **not** set
  `ESP_HOSTED_MEMPOOL_PREFER_SPIRAM` — PSRAM-backed DMA is unreliable under RF). To fit
  it alongside the display stack, the SDIO TX/RX queues are shrunk to 6 in
  `sdkconfig.defaults`. These are committed; don't revert without re-testing under load.

## Dev credentials + cloud (gitignored `sdkconfig`)

Wi-Fi + cloud config are project Kconfig options. The placeholders live in
`main/Kconfig.projbuild`; set real values in `sdkconfig` (gitignored — secrets stay
local) and rebuild:

```
CONFIG_DITTO_WIFI_SSID="<2.4GHz SSID>"        # the C6 is 2.4GHz-only
CONFIG_DITTO_WIFI_PASSWORD="<password>"
CONFIG_DITTO_API_BASE_URL="https://<your-deployment>.vercel.app"   # no trailing slash
CONFIG_DITTO_DEVICE_KEY="dvk_..."             # from admin: Add device -> Claim printer (shown once)
```

The device polls `GET /api/device/commands` (heartbeat → shows online in admin),
acks `identify`/`reboot`/`config-changed` commands, and accepts ESC/POS print jobs on
TCP `:9100` → renders → uploads to R2 → returns a public `/r/<token>` receipt URL.
The ESC/POS sender (`tools/escpos-harness/send.js`) must be on the **same LAN subnet**
as the device.

## Hardware validation log

**M5a config-driven renderer — verified on hardware 2026-06-16** (ESP32-P4 Waveshare 4B,
IDF 5.5, prod `ditto-admin-brown.vercel.app`):
- Boot → splash → config-driven render from cached/fetched config.
- Branding parity: edit brand colors + add a text object in admin → device repaints within
  a poll cycle (`config-changed` → refetch → re-render). Brand bg + **TTF text** render
  correctly; text crisp at large/small sizes.
- Receipt flow: `send.js` → render 576-wide PNG → `POST /api/ingest` 201 → on-screen QR →
  `/r/<token>` resolves (cert validated). Cycle idle → processing → qr confirmed live.
- Online status dot (firmware chrome) tracks connectivity.
- logo/icon/image objects render as faint placeholder outlines (real decode + asset cache
  is M5b); sent/error/paused/setup screens render from config but aren't yet driven by the
  state machine (later milestones).
- Memory-safety fixes that made this stable: status-dot cleared per render, locked
  `ui_set_online`/`ui_set_qr_url`/`ui_set_config`, per-build font-cache pinning, config
  scratch-swap double-buffer, and **`device_config_t` copies moved to PSRAM** (3×67KB in
  internal `.bss` had starved mbedtls → `-0x7F00` TLS alloc failures; PSRAM dropped DIRAM
  use 82%→36%).

**M5b uploaded logo/icon images — verified on hardware 2026-06-17** (same board/IDF/prod):
- Upload a PNG logo in admin branding → `config-changed` → device prefetches the presigned
  R2 asset to the `/assets` LittleFS partition → `render_image` decodes (lodepng → PSRAM via
  swapped image draw-buf handlers) → renders scaled-in-box (CONTAIN). Confirmed by eye.
- **Offline render-from-cache**: with the asset cached, dropping the network keeps the logo
  on screen — the render path is cache-only (`asset_cache_read_cached`, never networks).
- **Receipt-flow regression**: tap → idle→processing→QR→`/r/<token>` still resolves; M5b
  image work didn't disturb the M5a/M4 path.
- Decode is memory-bound: decoded bitmap = w·h·4 in PSRAM, so logos must be modestly sized.
  The admin now downscales uploads to ≤512px PNG server-side (ditto-admin) so the device
  always gets a decodable asset; oversized/SVG no longer reach the device.
- Gotchas fixed en route: `CFG_URL_LEN` 256→768 (presigned R2 URLs ~500 chars were truncated
  → R2 400), and `CONFIG_SPIRAM_USE_MALLOC=y` so the lodepng intermediate scanline buffer
  spills to PSRAM instead of failing on internal-RAM fragmentation. Online status dot removed
  (connectivity moves to the M5c wifi widget). Preset icons (src=PRESET) remain placeholders
  (deferred — needs an embedded sprite set).

**M5c-1 live widgets (clock/wifi/spinner) — verified on hardware 2026-06-18** (same board/IDF):
- **Clock + wifi confirmed on device.** Clock label ticks every second via an LVGL timer
  (`--:--` until SNTP syncs), formatted by the pure host-tested `format_clock`; time comes
  from SNTP (`time_sync` component, `esp_netif_sntp`), displayed in the tenant's timezone.
  Wifi bars track live RSSI (`esp_wifi_sta_get_rssi` → 0–4, pushed via `ui_set_wifi_level`);
  the branding wifi slider is preview-only.
- Spinner (`lv_spinner` on the processing screen) builds + is wired but was not eyeballed this
  pass (low-risk pure animation); confirm opportunistically on the next receipt.
- **Timezone needs the admin POSIX change deployed:** the device does `setenv(TZ, …)` with the
  payload's `clockTimezone`, which ditto-admin now converts IANA→POSIX in `getDeviceConfig`
  (`lib/posix-tz.ts`). Until that admin deploy lands, the device receives an IANA name newlib
  can't parse and the clock runs UTC. After deploy, changing the tenant timezone shifts the
  on-screen clock within a poll cycle.
- Widget lifecycle is concurrency-safe (clock timer deleted before screen free; wifi handle
  nulled per render — same pattern as the status dot). State→screen routing unchanged (M5a).

**M5c-2 receipt countdown + auto-return — verified on hardware 2026-06-18** (same board/IDF):
- The receipt/QR screen shows a live "Code expires M:SS" countdown + a depleting `lv_bar`
  (1 Hz LVGL timer); at 0:00 the device **auto-returns to idle**. Confirmed ticking +
  auto-return on device.
- Duration is tenant-configurable: admin `qrTimeoutSeconds` (15–180, default 60) →
  `device_config.qr_timeout_seconds`; the countdown widget + auto-return use it.
- Auto-return is driven by a UI expired-flag (`ui_consume_countdown_expired`), polled on a
  500 ms slice in `idle_wait_or_qr_expiry` (POLL_IDLE_MS is 12 s), NOT by `s_state` — so it
  works for both the tap-test path and the real ESC/POS `render_job` path. `s_cd_pending` is
  kept current by arming on every config apply.
- Known follow-up (pre-existing, M5d): `render_job_handle` doesn't set `s_state`, so a
  config-changed arriving during a live ESC/POS receipt QR can re-render it to idle early.

**M7 device policies (brightness / sleep / PIN-gated Settings) — verified on hardware 2026-06-21**
(same board/IDF 5.5; merged to main `9ef26dd`, flashed + HW-verified). Makes the firmware honor
the org-wide device settings the cloud delivers in `/api/device/config`'s new `device` block
(`brightness`, `sleep:{enabled,timeoutSeconds}`, `settingsPasswordHash`, `settingsPasswordSalt`).
Spec/plan in ditto-admin `docs/superpowers/{specs,plans}/2026-06-21-firmware-m7-device-policies*`.
Pure logic is host-tested in `tools/cfg-harness` (15/15): the `device`-block parser, `should_sleep`,
and `settings_pin_verify`.
- **Brightness** (`device.brightness`, clamped 10–100): applied via `bsp_display_brightness_set`
  on every config-apply and at boot (`main/app_state.c`, `display_apply_brightness`). Confirmed on
  device — serial logs `Setting LCD backlight: N%` matching the admin value; changing it in admin
  follows within a poll cycle.
- **Screen sleep** (`device.sleep`): blanks the backlight after `timeoutSeconds` of inactivity
  (pure `should_sleep` checked on the existing 500 ms `idle_wait_or_qr_expiry` slice), engages
  ONLY in `DEV_IDLE`. **Wakes** on any touch (new `ui_consume_activity` PRESSED flag) OR on a
  printed receipt (`app_state_set_state` wakes on PROCESSING/QR). CPU keeps polling while asleep
  (no deep sleep). Confirmed: blanks, wakes on touch (no spurious test-ingest, via the 800 ms wake
  guard), wakes + shows QR on a receipt printed while asleep, stays on when sleep disabled.
- **On-device Settings** (`device.settingsPassword*`): **long-press** the idle screen → a numeric
  PIN keypad (`components/ui/ui_settings.c`, passive — mirrors `ui_wifi.c`). PIN verified locally as
  `sha256(salt+pin)` (vendored Brad-Conte SHA-256, constant-time compare); ungated when no hash is
  set. Menu = device info (fw/SSID/bars/IP/status) + Wi-Fi setup (reuses `wifi_setup_run`) + Test
  print + Reboot. Confirmed: right/wrong/ungated PIN paths and all menu actions.
- **Concurrency** (sleep state is shared poll-task ↔ render_job task): `s_display_asleep`,
  `s_last_activity_ms`, `s_wake_guard_until` are `volatile`; a self-heal re-wake in the idle slice
  closes the sleep-vs-receipt race (recovers ≤500 ms). Review-caught bug fixed (`13c9669`):
  `bsp_display_backlight_on()` on this BSP == `brightness_set(100)`, so `display_wake` must set the
  configured duty LAST or it snaps to 100% on wake.
- Known non-blocking watch-items: BSP backlight writes from two tasks are unsynchronized (idempotent
  + self-healed); a long-press while asleep opens Settings only after the wake (up to one poll cycle
  latency); no idle timeout on the PIN gate (spec did not require one).

## Swipe-up Settings + on-device Device settings (2026-06-23, HW-verified)

Reworks how Settings opens and adds two sub-screens. Branch `feat/swipe-up-settings`.

- **Entry: swipe-up, not long-press.** A bottom-to-top swipe on the active screen (detected on
  release via the pure `gesture_is_swipe_up`, `components/devcfg/gesture.c`) opens Settings, which
  loads as a **full-screen `lv_screen_load_anim` MOVE_TOP** overlay (`scr`, owned by
  `run_settings_flow`; auto_del=false so ui.c's branding `s_active` is never freed under it). A
  finger-tracking bottom-sheet was prototyped then removed in favor of this simpler slide. The poll
  loop's `idle_wait_or_qr_expiry` peeks `ui_swipeup_pending()` so Settings opens within ~0.5 s, not
  after a full poll cycle. A swipe that merely wakes a sleeping screen does NOT open Settings
  (`ui_set_input_suppressed` held through the 800 ms wake-guard). A receipt mid-Settings sets
  `s_settings_abort`; render_job loads the QR and the flow deletes `scr` and returns.
- **Redesign + typeface.** PIN keypad (dot entry) + menu restyled to the dReceipt mockups, colors
  derived from the org brand via `lv_color_mix`. Device typeface switched to **Plus Jakarta Sans**
  (OFL), instanced to wght 400/700 and Latin-subset to ~23 KB each, same embedded filenames
  (`components/assets/fonts/ditto_sans*.ttf`) so the font cache picks it up with no code change.
- **About** (read-only): firmware / IP / MAC / SSID+signal / online / `:9100` listener, status dots.
- **Device settings** (`components/devcfg/overrides.c` = `device_settings_t`, NVS keys
  `dev_use/dev_br/dev_sl`): a single **"Use device settings"** switch picks the source for BOTH
  brightness and sleep — OFF follows the admin/cloud policy (controls disabled+dimmed), ON uses
  device-local values (seeded from the cloud on enable so nothing jumps). Brightness slider dims
  live and persists on RELEASE; sleep chips (Off/1/2/5/10 min) persist on tap. Fresh NVS
  (use_device=0) == byte-for-byte follow-admin.
- **Perceptual brightness curve** (`components/devcfg/brightness.c`): the stored/slider level
  (10..100) is gamma-curved (γ=0.5) to PWM duty so mid-slider isn't too dark (level 30→55% duty,
  50→71%), applied uniformly for device + admin brightness. Tunable via `BRIGHTNESS_GAMMA`.
- Host-tested pure cores: `gesture_is_swipe_up`/`gesture_snap`, `devset_*`, `brightness_level_to_duty`
  (`tools/cfg-harness`, `make test`). **CONFIRMED on hardware:** swipe opens Settings, brightness
  feels right at 30–40%, the use-device switch + persistence work, no boot regression (SDIO/Wi-Fi up).

## Icon→image unify + Wi-Fi widget parity (2026-07-20, HW-verified) — fw 0.8.0

Branch `feat/image-unify-widgets` (off main). Carries the two boot-bar fixes
(cherry-picked `ed9bc27..3e606ee` — gate-budget command timeout bounds splash
overshoot; touch/swipe suppressed while `s_boot_gate`; `boot_gate_fallback_offline`
helper) plus the widget/render changes below. Pairs with the cloud change retiring the
branding `icon` object type in favour of `image` (uploads only).
- **Wi-Fi widget parity** (`components/ui/ui.c` `render_wifi` + `ui_set_wifi_level`):
  bars match the admin branding preview — heights 0.45/0.65/0.85/1 of the box, width
  ~0.22×base, gap ~0.14×base, radius ~0.10×base, active/inactive opacity 217/51.
- **Wi-Fi drops on disconnect** (`main/app_state.c`): the `!net_is_connected()` poll
  branch calls `ui_set_wifi_level(0)` so the bars empty on outage instead of freezing
  "connected". **CONFIRMED on hardware** (b580): killing Wi-Fi empties the bars.
- **icon→image fallback** (`ui.c` dispatch): a stray legacy `OBJ_ICON` renders via the
  image path when `icon_url` is set; no preset/sprite code (the preset-icon branch
  `feat/m5b-preset-icons` was abandoned in favour of uploads-only).
- Clean `idf.py build`; per-task reviews clean; flashed to b580.

## Pinned QR (2026-07-22, HIL PASSED 2026-07-23) — fw 0.9.0

Branch `feat/pinned-qr`. Adds a persistent (non-expiring) `DEV_PINNED` QR state driven by a
cloud-set `pin_url`, distinct from the existing trigger-QR countdown flow. `s_qr_url`
(`components/ui/ui.c`) widened 256B→512B to match both `device_config_t.pin_url` and
`dev_command_t.url` (512B contracts); `ui_set_qr_url` now `ESP_LOGW`s when an incoming URL
would truncate.

- Host: `cfg_parse` pin block (set / null / absent / malformed / overlong URL); `home_state`;
  `should_sleep` with `DEV_PINNED`; `screen_for_state(DEV_PINNED)` — `tools/cfg-harness`, all pass.
- Build: clean `idf.py build`, no new warnings.
- **Pinned QR (0.9.0) — HIL PASSED 2026-07-23** (b580: OTA converge + pin command acked remotely + pinned render confirmed on-device). Original checklist:
  - Set pin from tenant UI → persistent QR (no countdown) within ~2 s.
  - Trigger → countdown QR → auto-return to pin.
  - Change URL → repaint.
  - Remove pin → idle.
  - Reboot → pin right after boot gate.
  - Screen-sleep still engages and wakes on touch.

## Pinned screen + styled QR (2026-07-22, HIL PASSED 2026-07-23) — fw 0.10.0

> HIL result: b580 OTA-converged to 0.10.0; styled rounded-module QR **scanned successfully by phone camera** (user-verified); pinned screen renders. Scannability gate closed.

Branch `feat/pinned-screen-styled-qr`. Two independent features:

- **Editable pinned screen** (`components/devcfg/{device_config,cfg_parse,render_geom}`):
  an 8th screen `SCREEN_PINNED` (index 7, appended last to match cloud's
  `PRINTER_SCREENS`) parses `config.screens.pinned` through the existing
  generic per-screen loop. `screen_for_state(DEV_PINNED)` stays a pure,
  cfg-unaware `SCREEN_QR` fallback (host-testable in isolation); the new
  cfg-aware `screen_for_pinned(cfg)` is what `ui_render_state` actually calls
  for `DEV_PINNED` — `SCREEN_PINNED` once the tenant has put objects on that
  screen, `SCREEN_QR` fallback otherwise (old cached configs, unedited
  screen). Countdown suppression for `DEV_PINNED` renders is unchanged
  (state-keyed, not screen-keyed).
- **Styled QR** (`components/ui/ui.c` `render_qr`, new `components/devcfg/qr_style.c`):
  replaced the plain `lv_qrcode` widget with a custom draw — `qrcodegen`
  encodes, then an `lv_canvas` renders dark modules as filled circles
  (~0.85× the module cell) and the three finder patterns as rounded-rect
  rings + dots (radius ~1/3 of each shape). One code path for every QR
  render (trigger/pinned/setup). The canvas pixel buffer is `heap_caps`
  PSRAM, freed on `LV_EVENT_DELETE` — same lifecycle as `render_image`'s
  decoded bitmaps; nothing lingers in PSRAM between screen builds. Pure
  module→pixel geometry (module size, dot diameter, finder radius,
  finder-cell test) lives in `qr_style.c`/`qr_style.h`, host-tested.
- **Wording**: setup-screen step 2 "Add a printer to a store" → "Add a
  screen to a store".

- Host: pinned-screen object parsing + `screen_for_pinned` (NULL/invalid/
  empty/populated cfg) + QR geometry helper (module size, canvas size, dot
  diameter, finder radius, finder-cell membership) — `tools/cfg-harness`,
  all pass.
- Build: clean `idf.py build`, no new warnings.
- **Pinned screen + styled QR (0.10.0) — pending HIL (scannability: 2 phones).**
  HIL checklist (b580):
  - Pinned screen with tenant-edited objects shows that layout (no countdown)
    when pinned; an unedited pinned screen still shows the QR-screen fallback.
  - Trigger QR and pinned QR both scan reliably with 2 phones (iOS + Android
    camera) at arm's length; if flaky, widen the dot diameter toward 1.0×
    (square-ish) and retest.
  - Setup screen reads "Add a screen to a store".
  - Version 0.10.0 reported on the heartbeat.

## QR style options (2026-07-23) — fw 0.11.0

Branch `feat/qr-style-options`. Cloud counterpart:
`docs/superpowers/specs/2026-07-23-qr-style-options.md` (ditto-admin).

Org-wide QR look (`config.qrShape`/`qrFg`/`qrBg` — siblings of
`clockTimezone`/`qrTimeoutSeconds` inside `"config"`, parsed in
`cfg_parse.c`, NOT document-root):

- `qrShape`: `"classic"` (square modules, square finder rings) | `"soft"`
  (rounded-square modules, r≈0.25× module) | `"rounded"` (today's ~0.85×
  circles — default for absent/unknown) | `"dots"` (~0.7× circles, tighter
  than rounded). Finder rings: square corners for classic, the existing
  rounded rings otherwise. New `qr_module_style()` in `qr_style.c` centralizes
  the per-shape module draw params (kind/size/offset/radius); `ui.c`'s
  `render_qr`/`draw_qr_finder` just dispatch on it.
- `qrFg`/`qrBg`: `"#rrggbb"`, default `#111111`/`#ffffff`. Canvas bg (incl.
  quiet zone) painted with `qrBg`; modules + finder rings with `qrFg`. Trusts
  the cloud's normalize-side contrast guard but keeps a defensive same-color
  check (`fg == bg` → both reset to defaults) so a bad payload can never
  render an invisible QR.
- Old cloud (fields absent) renders identically to 0.10.0 — defaults are
  exactly the pre-existing look.

- Host: `qr_shape_t`/color parse matrix (all 4 shapes, unknown shape,
  malformed hex, fg==bg) + per-shape geometry (dots diameter, soft radius,
  finder radius per shape, `qr_module_style` per shape) — `tools/cfg-harness`,
  all pass.
- Build: clean `idf.py build`, no new warnings.
- **QR style options (0.11.0) — pending HIL (scan all 4 shapes + a colored
  combo, 2 phones).**

## QR background corner + shadow (2026-07-23) — fw 0.12.0

Branch `feat/qr-corner-shadow`. Cloud counterpart: same doc's addendum
(`docs/superpowers/specs/2026-07-23-qr-style-options.md`, ditto-admin).

Independent of module `qrShape` — governs the QR's own background PLATE
(`config.qrCorner`/`qrShadow`, siblings of `qrShape`/`qrFg`/`qrBg`):

- `qrCorner`: `"square"` | `"rounded"` (default for absent/unknown — matches
  the pre-existing look). Rounded radius is ~6% of the canvas's pixel side
  (`qr_bg_radius_px()` in `qr_style.c`, mirrors the cloud's
  `lib/qr-svg.ts` `qrBackgroundRadius`).
- `qrShadow`: `bool` (default `false` for absent/non-bool).
- **Alpha approach chosen:** neither an ARGB8888 canvas nor a separate
  backing `lv_obj` plate. The QR canvas is built on LVGL's `lv_image_class`
  (`lv_canvas_class.base_class`), whose own draw path already reads the
  object's `radius` style as `draw_dsc.clip_radius` and masks the widget's
  own pixel content to a rounded rect at composite time (same mechanism
  `render_image` already uses for `icon_circle`). So `render_qr` just calls
  `lv_obj_set_style_radius(canvas, qr_bg_radius_px(canvas_px, qr_corner),
  LV_PART_MAIN)` (+ `clip_corner` for parity with the icon_circle call site)
  — the RGB565 pixel buffer and its full-square `qrBg` fill are unchanged;
  clipped-away corners simply show the screen behind. This avoids doubling
  the QR canvas's PSRAM footprint (ARGB8888 would be 2x RGB565) and avoids
  an extra widget/alloc per render.
- `qrShadow` uses `lv_obj_set_style_shadow_width/color/opa/offset_y` on the
  same canvas object: width 24px, color `#0F1428`, opacity 30%, y-offset
  4px — subtle, matches the cloud preview's `rgba(15,20,40,0.35)` drop
  shadow in spirit (LVGL has no SVG-style blur `stdDeviation`, so this is an
  approximation, not a pixel match). Shadow follows the same `radius` (so it
  hugs the rounded plate when `qrCorner: "rounded"`).
- Old cloud (fields absent) renders identically to 0.11.0 — square-cornered,
  no shadow, is what `qr_bg_radius_px`'s degenerate-canvas/default-corner
  cases guarantee.

- Host: `qr_bg_radius_px` geometry (rounded ~6%, square 0, degenerate
  canvas_px) + `qrCorner`/`qrShadow` parse matrix (absent, both values,
  explicit `false` kept, unknown corner, non-bool shadow) —
  `tools/cfg-harness`, all pass.
- Build: clean `idf.py build`, no new warnings.
- **QR background corner + shadow (0.12.0) — pending HIL (square vs rounded
  corner visible against a non-white screen bg, shadow on/off, 2 phones for
  scan regression).**

## QR shadow mode/strength/color (2026-07-24) — fw 0.13.0

Branch `feat/qr-shadow-neon`. Device side of the cloud's QR shadow modes;
cloud counterpart pending. Replaces the plain boolean `qrShadow` from
0.12.0 with a tunable shadow (`config.qrShadowMode`/`qrShadowStrength`/
`qrShadowColor`, siblings of `qrCorner`):

- `qrShadowMode`: `"none"` (no shadow, default for absent/unknown) |
  `"drop"` (soft shadow behind-and-below the plate — today's look, now
  tunable) | `"neon"` (fuller, brighter surrounding glow, no downward
  offset). Older cloud payloads sent only the boolean `qrShadow`;
  `cfg_parse.c` migrates `qrShadow: true` to `"drop"`, but only when
  `qrShadowMode` itself is absent from the payload — a present-but-unknown
  mode string is "unknown", not "absent", and stays `"none"` (no legacy
  fallback in that case; a modern `qrShadowMode` always wins over a legacy
  `qrShadow` when both are present).
- `qrShadowStrength`: `0..100` int, default `50` for absent, clamped to
  range for out-of-range/non-number values.
- `qrShadowColor`: `"#rrggbb"`, default `#000000` for absent/malformed.
- New pure curves in `qr_style.c` keep `ui.c` thin: `qr_shadow_width_px`
  (linear `8..48`px, mode-independent) and `qr_shadow_opa_pct` (linear
  `20..90` for DROP, `35..100` for NEON — NEON is brighter than DROP at
  every strength). `render_qr` dispatches on `qr_shadow_mode`: DROP sets
  `shadow_width`/`shadow_color`/`shadow_opa` from those curves plus a small
  downward `shadow_offset_y` (`width/6`, "resting on the surface"); NEON
  uses `offset_y = 0` (glow surrounds evenly), `width` widened `~1.5x`, and
  the brighter NEON opacity curve — both read as "neon" rather than "drop
  shadow". NONE draws no shadow (canvas keeps its default no-shadow style).
- Old cloud (fields absent) renders identically to 0.12.0's no-shadow
  default — `qrShadowMode` absent + no legacy `qrShadow` -> NONE.

- Host: `qrShadowMode`/`qrShadowStrength`/`qrShadowColor` parse matrix
  (absent, each mode, unknown mode, legacy-bool migration in both
  directions incl. modern-wins-over-legacy, strength clamping, malformed
  color) + `qr_shadow_width_px`/`qr_shadow_opa_pct` curves (endpoints,
  midpoint, NEON>DROP invariant, out-of-range clamping) —
  `tools/cfg-harness`, all pass.
- Build: clean `idf.py build`, no new warnings.
- **QR shadow mode/strength/color (0.13.0) — pending HIL (drop vs neon vs
  none visibly distinct against a non-white screen bg, strength slider
  low/mid/high, custom shadow color, scan regression, 2 phones).**

## QR corner + shadow parity formulas (2026-07-24) — fw 0.14.0

Branch `feat/qr-parity-formulas`. User report: the device QR plate's shadow
was "barely visible" and its corner radius didn't match the branding studio
preview. Root cause: the 0.12.0/0.13.0 curves (`qr_bg_radius_px`'s fixed 6%,
`qr_shadow_width_px`'s fixed `8..48px`, `qr_shadow_opa_pct`'s fixed
`20..90%`/`35..100%`) were sized for one reference canvas and didn't scale
with how large the QR plate actually renders — a small device canvas got a
proportionally tiny shadow/radius next to the studio's larger preview
canvas, even at identical corner/strength settings. Fix: port the cloud's
canonical *dim-relative* formulas (`ditto-admin lib/qr-svg.ts`
`qrCornerRadiusPx`/`qrShadowParams`, added there the same day) exactly,
`dim` = the plate's own rendered pixel size (`canvas_px`) on both sides:

- `config.qrCorner` (`"square"` | `"rounded"` enum) is replaced by
  `config.qrCornerRadius`, a continuous `0..100` int slider (`0` = square).
  `cfg_parse.c`'s `qr_corner_radius_of` mirrors the cloud's
  `sanitizeQrCornerRadius`: a valid `qrCornerRadius` number always wins
  (clamped+rounded); otherwise a legacy `qrCorner` string maps 1:1
  (`"rounded"` -> `24`, `"square"` -> `0`); anything else (absent, garbage,
  unrecognized legacy value) -> the default (`24`, today's live look —
  unchanged visually for existing tenants).
- `qr_style.c`'s `qr_corner_radius_px(dim, v) = round(dim * 0.30 * v/100)`
  replaces `qr_bg_radius_px`.
- `qr_style.c`'s `qr_shadow_params(mode, strength, dim)` replaces
  `qr_shadow_width_px`/`qr_shadow_opa_pct`: `blurPx = dim * (0.04 +
  0.20*s)` (`s = strength/100`); DROP `offsetY = dim*0.02`, `opa =
  round((0.25 + 0.50*s) * 255)`; NEON `offsetY = 0`, `opa = 255` (full-color
  halo), `spreadPx = 2x the UNROUNDED blur` (not `2 * blur_px`, so it
  doesn't drift from an intermediate rounding step — matches the cloud's
  float math exactly). `render_qr` maps these onto LVGL styles: DROP
  `shadow_width = blurPx*2` (LVGL's shadow blur extends on both sides of the
  base rect, unlike CSS's one-sided blur-radius, hence the `*2`
  calibration), `ofs_y = offsetYPx`, `opa` direct; NEON `shadow_width =
  spreadPx*2`, `ofs 0`, `opa 255`, plus `shadow_spread = blurPx/2` — the
  blur alone still read weak at full opacity in review, so NEON's base rect
  is additionally widened for a fuller halo.
- `qr_corner_radius_px`/`qr_shadow_params` are the pure geometry — `ui.c`
  only maps their outputs onto LVGL style setters, no math of its own.
- Old cloud (`qrCornerRadius`/`qrShadowMode`/etc. absent) still renders
  identically to 0.13.0's defaults — `qr_corner_radius_of`'s absent/garbage
  fallback and `qr_shadow_params`'s `QR_SHADOW_NONE` short-circuit guarantee
  it.

- Host: `qrCornerRadius` parse matrix (value/clamp/absent/legacy both
  directions, modern-wins-over-legacy) + `qr_corner_radius_px`/
  `qr_shadow_params` concrete-value matrix (the spec's `dim=260` examples,
  dim-scaling, strength endpoints/clamping, degenerate/negative `dim`) —
  `tools/cfg-harness`, all pass.
- Build: clean `idf.py build`, no new warnings.
- **QR corner + shadow parity formulas (0.14.0) — pending HIL (parity vs
  studio preview: corner radius visually matches at several slider values,
  drop shadow now visibly present vs 0.13.0, neon glow reads as a fuller
  halo, scan regression, 2 phones).**

Also fw 0.15.0 (`fix(ui): hard-cap LVGL qr shadow width/spread`): the
`QR_SHADOW_WIDTH_MAX`/`QR_SHADOW_SPREAD_MAX` caps (64px/16px) in
`render_qr` — an *uncapped* dim-relative `shadow_width`
(`qr_shadow_params`'s NEON `spreadPx*2`, ~250px on a 260px plate) wedged
LVGL's software shadow-blur render for seconds per frame while holding the
LVGL lock, which starved `poll_task` and read as a boot hang. The caps kept
NEON usable but capped its visual reach well below the cloud preview's — the
problem the canvas-glow rewrite below replaces the whole mechanism to fix
properly rather than raising the cap.

## NEON pre-rendered canvas glow (2026-07-24) — fw 0.16.0

Branch `fix/neon-canvas-glow`. Problem: even after the 0.15.0 hard-cap fix,
NEON's device glow still didn't match the branding studio preview — (a) the
64px `shadow_width` cap means a strong glow (dim-relative `spreadPx` can
reach 100+px on a large plate) visibly undershoots the cloud's halo reach,
and (b) LVGL's `shadow_width`/`shadow_spread` is a single Gaussian-ish blur
pass, not the preview's richer two-layer glow (a bright, tight inner bloom
stacked under a wider, dimmer outer bloom). Raising the cap was explicitly
ruled out (0.15.0 boot-hang incident above) — LVGL's *live* software shadow
blur cost grows superlinearly with width, so a wide NEON halo can never be
cheap as a per-frame effect on this hardware.

Fix: stop using a live LVGL shadow for NEON entirely. Pre-render the glow
into the QR canvas's own (enlarged) pixel buffer ONCE, at canvas-build time
— zero per-frame cost, so the halo can be as wide as the cloud preview's
without touching the render-loop budget. DROP and NONE are untouched (same
plate-sized RGB565 canvas + LVGL `shadow_*` style path as 0.12.0-0.15.0).

- `components/devcfg/qr_style.c`/`.h` gain three pure, host-testable
  functions (no LVGL/qrcodegen dependency):
  - `qr_neon_halo_px(plate_px, spread_px, max_side_px)` — turns
    `qr_shadow_params(NEON,...).spread_px` (the CLOUD PARITY halo reach)
    into the enlarged canvas's outward margin, capped so the halo never
    exceeds the plate's own size and the final canvas side
    (`plate + 2*halo`) never exceeds `QR_NEON_CANVAS_SIDE_MAX_PX` (720 —
    the panel's own side).
  - `qr_glow_dist_px(px, py, rect_w, rect_h, r)` — the standard
    rounded-box signed-distance-field (Inigo Quilez's `sdRoundBox`),
    clamped to the OUTSIDE distance only (0 inside/on-boundary). Lets the
    glow follow the plate's own `qr_corner_radius_px` rounding instead of
    a plain rectangle.
  - `qr_glow_alpha_at(d, blur_px, spread_px)` — the two-layer falloff:
    `a1=(1-d/blur)^2` for `d<blur` (tight inner glow) stacked with
    `a2=0.55*(1-d/spread)^2` for `d<spread` (wide outer glow, capped at
    55% peak so it reads as a dimmer bloom, not a second bright ring);
    `alpha = 255*max(a1,a2)`.
- `components/ui/ui.c`'s `render_qr`, NEON path only: computes
  `glow = qr_shadow_params(NEON, strength, canvas_px)` and
  `halo = qr_neon_halo_px(canvas_px, glow.spread_px, 720)`, allocates an
  `ARGB8888` canvas of side `canvas_px + 2*halo` (vs. the plain
  `RGB565`/`canvas_px`-sized canvas for DROP/NONE), fills it fully
  transparent, then a new `paint_qr_neon_glow` helper writes the glow into
  the raw pixel buffer directly (bypassing `lv_canvas_set_px`'s
  per-pixel `lv_obj_invalidate`, since this is a ~270k-pixel loop) using
  `qr_glow_dist_px`/`qr_glow_alpha_at`. The plate (rounded `qrBg` rect) and
  all modules/finders are then drawn as before but offset by `halo` on both
  axes, so the QR itself still lands exactly centered in the object box —
  only the canvas grew, not the box position math. For NEON, the lv_obj
  `radius`/`clip_corner` style and the LVGL `shadow_*` styles are skipped
  entirely (clipping would cut off the glow past the plate; the plate's own
  rounding is already drawn in-canvas). DROP/NONE keep the exact 0.14.0/
  0.15.0 code path (whole-canvas `qr_bg` fill, lv_obj radius+clip, DROP's
  `shadow_*` styles capped at `QR_SHADOW_WIDTH_MAX`=64px).
- Quiet zone / module layout / scan geometry are untouched — `module_px`,
  `canvas_px`, and `QR_QUIET_MODULES` are computed exactly as before; only
  where the plate is drawn *within* the (possibly enlarged) canvas moved.
- PSRAM cost at plate≈260px, `strength=100` (`s=1.0`, the worst case —
  `blur=62px`, `spread=125px`): `halo=125px` (well under the plate-size
  cap), canvas side `260+2*125=510px`, buffer `510*510*4 ≈ 1.04MB`. At
  `strength=50` (the `qr_shadow_params` spec's own dim=260 example,
  `blur=36`, `spread=73`): `halo=73px`, side `406px`, buffer `≈0.66MB`. Both
  comfortably within PSRAM budget; freed via the existing
  `on_qr_canvas_delete`/`LV_EVENT_DELETE` path unchanged (it already frees
  whatever pointer it's handed, regardless of size/format).

- Host: `qr_neon_halo_px` (spec example unaffected by either cap,
  spread>plate binds the plate cap, plate+2*halo>max_side_px binds the
  side cap, degenerate plate/negative spread, plate alone already over
  max_side_px) + `qr_glow_dist_px` (dead center inside, square-corner
  point exactly on the boundary, the SAME point read as outside once
  rounded — proves the falloff follows the rounded contour not the
  bounding box, flat-edge distance unaffected by radius, diagonal-corner
  Euclidean vs. naive-Manhattan distinction, degenerate rect) +
  `qr_glow_alpha_at` (d<=0 short-circuit, d==blur lands in the a2-only
  region, d==spread and beyond -> 0, monotonic decrease, degenerate
  blur/spread -> no div-by-zero) — `tools/cfg-harness`, all pass.
- Build: clean `idf.py build`, no new warnings. IDF 5.5 only.
- **NEON pre-rendered canvas glow (0.16.0) — pending HIL (neon halo reach
  now visibly matches the studio preview at high strength vs. 0.15.0's
  capped look, two-layer glow reads as a bright core + soft wide bloom
  rather than a single flat blur, DROP/NONE visually unchanged from
  0.15.0, scan regression, corner-radius slider still visually matches
  with NEON on, 2 phones). NOT flashed — build-verified only.**
