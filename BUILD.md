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
