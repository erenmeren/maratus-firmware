#pragma once
#include <stdbool.h>
// Block until the user picks a network and connects; persists creds to NVS.
// allow_exit: shows a cancel/exit chip on the network-list stage and lets the
// caller bail out without touching NVS creds or the current connection —
// pass false on first boot (nothing to fall back to), true from Settings
// (the caller's own menu rebuild handles the return).
void wifi_setup_run(bool allow_exit);
