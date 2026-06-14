# Building & flashing ditto-firmware

## Toolchain: ESP-IDF **5.4.x** (required)

The Waveshare board BSP (`waveshare/esp32_p4_wifi6_touch_lcd_4b`), the ST7703 panel
driver, the GT911 touch driver, and esp-hosted/esp_wifi_remote all target **ESP-IDF 5.4**.
**Do not build on IDF 6.0** — its `esp_lcd` MIPI-DSI API changes break the vendored panel
driver (and `json`/`usb`/driver-split changes ripple through the BSP).

```bash
source ~/.espressif/tools/activate_idf_v5.4.4.sh    # or your 5.4 export.sh
```

## Known post-fetch patch (esp_wifi_remote)

The latest `esp_wifi_remote` ships a Kconfig that omits the `WIFI_RMT_*` symbols its own
`WIFI_INIT_CONFIG_DEFAULT()` macro uses, so a fresh build fails in `net.c`. After the first
`idf.py build` fetches `managed_components/`, apply `tools/patch-deps.sh` (re-run it any time
`managed_components/` is re-fetched):

```bash
./tools/patch-deps.sh
```

## Build

```bash
idf.py set-target esp32p4
idf.py build            # if it fails in esp_wifi_remote, run ./tools/patch-deps.sh then rebuild
```

## Flash + monitor (USB-to-UART port, NOT USB-OTG)

Use the **USB-to-UART** Type-C port. Because the app owns USB, auto-reset into the bootloader
often fails — enter download mode manually:

1. Unplug USB-C → hold **BOOT** → replug while holding → release after ~2s.
2. The board enumerates on a new `/dev/cu.usbmodem*` (the ROM USB-Serial-JTAG).

```bash
idf.py -p <download-mode-port> flash       # writes; hard-resets into the app
idf.py -p <app-port> monitor               # UART0 console; Ctrl-] to exit
```
