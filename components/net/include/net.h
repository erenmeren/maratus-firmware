#pragma once

#include <stdbool.h>

// Wi-Fi connectivity (STA) via the ESP32-C6 radio. On the ESP32-P4 the Wi-Fi
// MAC/PHY live on the C6 co-processor; the `esp_wifi_remote` component routes
// the standard esp_wifi_* API to it over esp-hosted, so this is ordinary STA code.

// Start Wi-Fi and connect using the Kconfig dev credentials. Blocks up to ~20s
// for an IP, then returns (auto-reconnect stays armed via the event handler).
void net_start(void);

// True once an IP has been acquired (cleared on disconnect).
bool net_is_connected(void);
