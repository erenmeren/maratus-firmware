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
