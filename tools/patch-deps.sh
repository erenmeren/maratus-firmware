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
# NOTE: the GT911 touch-skip patch was removed (2026-06-15). On IDF 5.4.4 the
# GT911 I2C transaction busy-waited forever and hung main, so touch was skipped
# for display bring-up. On IDF 5.5 the GT911 inits cleanly (TouchPad_ID 0x39,31,31)
# with no hang, so real touch is kept — the BSP's stock touch init is used as-is.

# --- lvgl lodepng: route intermediate decode buffers to PSRAM ---
# LVGL's lodepng routes its decoded OUTPUT bitmap to PSRAM (via the image
# draw-buf handlers we install in ui.c) but its INTERMEDIATE buffers (zlib
# inflate + filtered scanlines, ~w*h*4 for the raw image) go through
# lodepng_malloc -> lv_malloc -> C malloc -> internal DRAM. On the ESP32-P4
# (~768KB internal SRAM) a >=512px image needs a ~1MB temporary that internal
# RAM cannot hold, so lodepng_decode fails and only the placeholder frame is
# drawn (silent: CONFIG_LV_USE_LOG is off). Route those temporaries to PSRAM.
LODEPNG="$ROOT/managed_components/lvgl__lvgl/src/libs/lodepng/lodepng.c"
if [ -f "$LODEPNG" ]; then
  if grep -q "PATCH (Ditto) lodepng PSRAM" "$LODEPNG"; then
    echo "already patched: $LODEPNG"
  else
    python3 - "$LODEPNG" <<'PY'
import sys
f = sys.argv[1]; s = open(f).read()
anchor = '#define image_cache_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers)'
inc = '''
/* PATCH (Ditto) lodepng PSRAM: route lodepng's intermediate decode buffers
   (zlib inflate + filtered scanlines, ~w*h*4 for the raw image) to PSRAM.
   The default lv_malloc -> C malloc lands in internal DRAM, which on the
   ESP32-P4 (~768KB internal SRAM) cannot hold the ~1MB temporary a >=512px
   image needs -> decode silently fails, only the placeholder frame shows. */
#include "esp_heap_caps.h"'''
assert anchor in s, "lodepng anchor not found"
s = s.replace(anchor, anchor + "\n" + inc, 1)
assert s.count("    return lv_malloc(size);") == 1
assert s.count("    return lv_realloc(ptr, new_size);") == 1
assert s.count("    lv_free(ptr);") == 1
s = s.replace("    return lv_malloc(size);",
              "    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);", 1)
s = s.replace("    return lv_realloc(ptr, new_size);",
              "    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);", 1)
s = s.replace("    lv_free(ptr);",
              "    heap_caps_free(ptr);", 1)
open(f, "w").write(s); print("patched:", f)
PY
  fi
fi

echo "patch-deps done."
