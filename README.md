# ditto-firmware

Firmware for the **Ditto** digital-receipt printer — a Waveshare **ESP32-P4-WIFI6-Touch-LCD-4B**
that receives Epson ESC/POS print jobs over Wi-Fi, renders them to a PNG, uploads the image
to Ditto Cloud, and shows the customer a QR code linking to the digital receipt.

- **SoC:** ESP32-P4 (dual RISC-V) + ESP32-C6 Wi-Fi 6 co-processor (SDIO)
- **Display:** 720×720 MIPI-DSI capacitive touch
- **Stack:** ESP-IDF v5.4+, LVGL v9

## Build & flash

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

## Status

Bring-up follows milestones M2–M6. See the architecture spec and the per-milestone plans in
the `ditto-admin` repo:

- Spec: `ditto-admin/docs/superpowers/specs/2026-06-14-ditto-firmware-design.md`
- M2 plan: `ditto-admin/docs/superpowers/plans/2026-06-14-firmware-m2-skeleton.md`

Vendor BSP facts (display/touch/Wi-Fi init) are recorded in [`docs/bsp-notes.md`](docs/bsp-notes.md).
