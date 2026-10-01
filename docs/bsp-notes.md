# BSP ground-truth notes (fill in during M2 Task 1)

Captured from the Waveshare **ESP32-P4-WIFI6-Touch-LCD-4B** ESP-IDF demo. These are the
vendor-specific facts the M2 plan defers to (display, touch, C6 Wi-Fi). Fill each in once
the demo builds + runs on the board.

- **ESP-IDF version the demo targets:** _TODO_
- **Display init entry point** (BSP component name or `esp_lcd`/LVGL init function): _TODO_
- **LVGL lock/unlock API** (e.g. `bsp_display_lock(0)` / `bsp_display_unlock()` or
  `lvgl_port_lock` / `lvgl_port_unlock`): _TODO_
- **Touch init entry point + touch controller IC** (e.g. GT911): _TODO_
- **C6 Wi-Fi bring-up** (managed component name — `esp_wifi_remote` / `esp_hosted` — and any
  SDIO pin/transport config the demo sets before `esp_wifi_init`): _TODO_
- **Managed components + versions** used by the demo (LVGL version, display component,
  Wi-Fi component) → mirror into `main/idf_component.yml`: _TODO_
- **sdkconfig options** enabling PSRAM + the C6 transport + MIPI-DSI → append to
  `sdkconfig.defaults`: _TODO_
- **Serial flash port** on this machine (e.g. `/dev/cu.usbserial-*`): _TODO_

Reference spec: `maratus-admin/docs/superpowers/specs/2026-06-14-maratus-firmware-design.md`.
