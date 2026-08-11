#pragma once
#include <stdbool.h>
// Block until the user picks a network and connects; persists creds to NVS.
// allow_exit: shows a cancel/exit chip on the network-list stage and lets the
// caller bail out without touching NVS creds or the current connection —
// pass false on first boot (nothing to fall back to), true from Settings
// (the caller's own menu rebuild handles the return).
void wifi_setup_run(bool allow_exit);

// Same picker, opened by the boot path when the stored credentials don't reach
// a network. Returns on its own after `idle_timeout_ms` if nobody has started
// picking one, so an unattended device isn't held on a setup screen while its
// router is merely slow to come back — boot just continues offline and the
// background reconnect keeps trying. Always exitable.
void wifi_setup_run_recovery(int idle_timeout_ms);
