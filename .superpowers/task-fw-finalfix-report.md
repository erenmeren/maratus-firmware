# Final-review fix wave — feat/pinned-qr

Branch verified: `feat/pinned-qr` (HEAD was `7d5debc build: bump fw to 0.9.0 + BUILD.md pinned-QR entry`), matched task's required branch — proceeded.

## Fix 1 (Important) — config fetch failure after ack loses the repaint

`main/app_state.c`, `poll_task`, config-dirty block (~line 514-536):

```c
if (s_config_dirty) {
    s_config_dirty = false;
    int next = s_cfg_live ^ 1;
    *s_cfg_buf[next] = *s_cfg_buf[s_cfg_live];
    int cs = cloud_get_config(s_cfg_buf[next]);
    if (cs == 200 && s_cfg_buf[next]->valid) {
        ... apply + repaint ...
    } else if (cs != 304) {
        // Neither a fresh config applied nor "not modified" — a transport
        // error or a 200 that failed to parse. Re-arm so the next poll
        // retries; otherwise an acked pin/config-changed command that lost
        // this one fetch to a blip would never repaint until some other
        // external nudge sets s_config_dirty again.
        s_config_dirty = true;
    }
}
```

- `s_config_dirty` is now only left `false` when the fetch either applied a
  fresh valid config (`cs==200 && valid`) or returned `304 Not Modified`
  (nothing changed server-side, so nothing to retry).
- Any other outcome — transport error (`cs==-1`), a non-200/304 HTTP status,
  or a `200` that failed to parse (`valid==false`) — re-arms
  `s_config_dirty`, so the next poll iteration (bounded by the existing
  poll cadence, ~12s) retries automatically. A permanently-failing fetch
  just keeps retrying every cycle, which is the accepted degraded mode
  (matches how the rest of poll_task already treats transient failures —
  no new unbounded-retry or crash-loop risk).
- **304-path verification**: read `cloud_get_config` (`components/cloud/cloud.c:323`)
  — on `status==304` it only logs "config not modified" and returns 304;
  `cfg->valid`/`pin_set`/etc. are never touched (the idle buffer was already
  seeded from the live buffer via the `*s_cfg_buf[next] = *s_cfg_buf[s_cfg_live]`
  copy one line above). The new `else if (cs != 304)` guard means the 304
  case falls through doing nothing — identical to pre-fix behavior. Confirmed
  by diff: the only new lines are inside the `else if`, no changes to the
  200/304 branches themselves.

## Fix 2 (Minor) — stale s_qr_url when going home unpinned

`main/app_state.c`, `show_home()` (~line 381-386):

```c
static void show_home(void) {
    const device_config_t *cfg = s_cfg_buf[s_cfg_live];
    bool pinned = cfg && cfg->valid && cfg->pin_set;
    if (pinned) ui_set_qr_url(cfg->pin_url);
    else ui_set_qr_url(NULL);   // clear stale pin/trigger url so an idle OBJ_QR can't render it
    s_state = home_state(pinned);
    ui_render_state(s_state);
}
```

Checked `ui_set_qr_url` (`components/ui/ui.c:656`) first: it already handles
`NULL` safely — `if (!url) { s_qr_url[0] = '\0'; }` — so no need for the
`ui_set_qr_url("")` fallback suggested as a contingency; `NULL` clears the
buffer directly and the renderer's existing empty-string fallback path
takes over on the next idle render.

## Fix 3 (Minor) — two host tests locking pin-parse invariants

`tools/cfg-harness/test_cfg.c`, `test_pin_block()` — added (f), (g), (h)
after the existing (e) truncation case:

- (f) re-parse a pin-less JSON into `c1` (previously pinned by case (a))
  → `c1.pin_set == false`. Locks in that a stale pin can't leak forward
  into the next poll's config once the cloud drops it. (Traced this to
  `cfg_parse_json`'s `memset(cfg, 0, sizeof(*cfg))` at function entry —
  the invariant already held structurally; this test pins it down as a
  regression guard.)
- (g) `{"pin":42}` (non-object) → `cJSON_IsObject(pin)` is false, block
  skipped entirely → `pin_set` stays false (from the memset).
- (h) `{"pin":{"url":123}}` (non-string url) → `cJSON_GetStringValue`
  returns NULL for a non-string node → `purl` is NULL → the
  `purl && purl[0]` guard fails → `pin_set` stays false.

Followed existing lettered-case comment style in the same function.

## Gates

### 1. Host test harness

```
cd tools/cfg-harness && make && ./run
```

Tail:
```
test_pin_block OK
test_mqtt_block OK
test_mqtt_topics OK
test_provisioning OK
test_wifi_util OK
test_ota_manifest OK
test_should_sleep OK
test_sha256_and_pin OK
test_gesture_swipe_up OK
test_gesture_snap OK
test_devset OK
test_brightness OK
test_trigger_show_qr OK
test_command_parse_into OK
ALL TESTS PASSED
```

### 2. Firmware build

```
. ~/.espressif/v5.5/esp-idf/export.sh && idf.py build
```

Tail:
```
[10/12] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32p4 image...
Merged 3 ELF sections
Successfully created esp32p4 image.
Generated /Users/eren/Projects/maratus-firmware/build/maratus-firmware.bin
[11/12] cd .../esptool_py && python .../check_sizes.py ...
maratus-firmware.bin binary size 0x195b30 bytes. Smallest app partition is 0x200000 bytes. 0x6a4d0 bytes (21%) free.

Project build complete.
```

Grepped full build log for "warning" (case-insensitive, excluding the
esp_hosted/NOTICE dependency-resolution lines) — 0 matches. Clean build,
no new warnings.

## Commit

Single commit, explicit `git add` of the three touched files:
`main/app_state.c`, `tools/cfg-harness/test_cfg.c`.
