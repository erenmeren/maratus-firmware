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

echo "patch-deps done."
