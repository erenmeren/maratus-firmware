#!/usr/bin/env bash
# Re-apply source patches to fetched managed_components (idempotent).
# Run after the first `idf.py build` (or any time managed_components/ is re-fetched).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- esp_wifi_remote: add the WIFI_RMT_* defaults its Kconfig stub omits ---
for HDR in "$ROOT"/managed_components/espressif__esp_wifi_remote/idf_v*/include/injected/esp_wifi.h; do
  [ -f "$HDR" ] || continue
  if grep -q "PATCH (Ditto)" "$HDR"; then echo "already patched: $HDR"; continue; fi
  python3 - "$HDR" <<'PY'
import sys
f = sys.argv[1]; s = open(f).read()
anchor = "#define WIFI_INIT_CONFIG_DEFAULT() { \\"
block = """/* PATCH (Ditto): esp_wifi_remote's Kconfig stub omits these WIFI_RMT_* symbols
   that the macro below uses. Provide standard IDF Wi-Fi defaults. */
#ifndef CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM 10
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM 32
#endif
#ifndef CONFIG_WIFI_RMT_TX_BUFFER_TYPE
#define CONFIG_WIFI_RMT_TX_BUFFER_TYPE 1
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF
#define CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF 0
#endif
#ifndef CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM
#define CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM 7
#endif

"""
if anchor in s:
    open(f,"w").write(s.replace(anchor, block+anchor, 1)); print("patched:", f)
else:
    print("anchor not found (skipped):", f)
PY
done
# --- Waveshare BSP: make touch (GT911) non-fatal for display bring-up ---
# Its I2C transaction busy-waits forever on this build (no ACK / bus held) and
# hangs main; and bsp_display_start_with_config asserts if indev is NULL. Skip
# touch and tolerate a NULL indev so the display still comes up.
BSP="$ROOT/managed_components/waveshare__esp32_p4_wifi6_touch_lcd_4b/esp32_p4_wifi6_touch_lcd_4b.c"
if [ -f "$BSP" ]; then
  python3 - "$BSP" <<'PY'
import sys
f=sys.argv[1]; s=open(f).read(); changed=False
old_touch="""    esp_lcd_touch_handle_t tp;
    BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(NULL, &tp));
    assert(tp);"""
new_touch="""    esp_lcd_touch_handle_t tp = NULL;
    (void)tp; (void)disp;  /* PATCH (Ditto): skip GT911 touch (I2C hangs); display-only */
    ESP_LOGW(TAG, "touch disabled (Ditto display bring-up)");
    return NULL;"""
if "skip GT911 touch" not in s and old_touch in s:
    s=s.replace(old_touch,new_touch,1); changed=True
old_chk="    BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(disp), NULL);"
new_chk="    disp_indev = bsp_display_indev_init(disp);  /* PATCH (Ditto): tolerate no-touch */"
if "tolerate no-touch" not in s and old_chk in s:
    s=s.replace(old_chk,new_chk,1); changed=True
if changed: open(f,"w").write(s); print("patched BSP touch:", f)
else: print("BSP touch already patched or anchors not found")
PY
fi

echo "patch-deps done."
