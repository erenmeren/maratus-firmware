# ESP-MQTT Device Transport — Design (maratus-firmware)

**Date:** 2026-07-16
**Status:** Approved (brainstorm complete)
**Scope:** Add an MQTT client transport to the ESP32-P4 firmware so device
commands arrive push-style (sub-second) instead of via the 12 s HTTP poll,
with HTTP polling kept as an automatic fallback. One milestone.
**Cloud contract:** the cloud side is already built + merged
(`maratus-admin` main, EMQX Cloud Serverless). This device implements the
**Path A** auth model (device-key built-in-DB auth), NOT the JWT described in
the older cloud design doc.

## Goal

Cut trigger latency from ~6 s average (0–12 s HTTP poll) to **< 1 s** by
receiving `show_qr` (and other) commands over MQTT, and cut idle HTTP traffic
to ~zero while connected. Old cloud (no `mqtt` config block) and broker
outages must degrade cleanly to today's HTTP polling.

## Cloud-facing contract (what the device must implement)

- **Broker:** host/port from the `/api/device/config` `mqtt` block, TLS (8883),
  server-cert verification via the existing mbedTLS cert bundle.
- **Auth (Path A, no JWT):** `client_id = username = deviceId` (both from the
  config `mqtt` block), `password = the device key` (already in NVS, the same
  key used for the HTTP `Authorization: Bearer` header).
- **Topics (QoS 1):** subscribe `d/{deviceId}/cmd`; publish `d/{deviceId}/ack`
  and `d/{deviceId}/hb`.
- **Command payload** (`d/{id}/cmd`): `{ commandId, type, action, payload }` —
  identical shape to today's HTTP poll-response command object.
- **Ack payload** (`d/{id}/ack`): `{ commandId, ok, result? }` — identical to
  today's HTTP ack body.
- **Heartbeat payload** (`d/{id}/hb`): `{ version }` (firmware version).
- **Presence:** none app-side — EMQX emits connect/disconnect events itself;
  the device only needs a sane keepalive so disconnects are detected promptly.

## Architecture

### New component: `components/mqtt/`

- `mqtt_client.c` — esp-mqtt glue: `mqtt_start(const device_config_t*)`,
  `mqtt_stop()`, `mqtt_is_connected()`, the esp-mqtt event handler, and publish
  helpers `mqtt_publish_ack(command_id, ok)` / `mqtt_publish_hb(version)`.
- `mqtt_topics.c` / `.h` (pure, host-testable) — builds `d/{id}/cmd|ack|hb`.
- `CMakeLists.txt`: `REQUIRES mqtt esp-tls appcfg devcfg net json` (+ whatever
  the command dispatch needs). esp-mqtt (`esp_mqtt_client`) is a bundled IDF
  5.5.4 component; its Kconfig (`MQTT_PROTOCOL_311`, `MQTT_TRANSPORT_SSL`) is
  already materialized in `sdkconfig`.

### Transport-agnostic command dispatch (`components/cloud/commands.c`)

Today `commands_handle_body()` reads each command straight from the transient
cJSON tree and acks inline via `cloud_ack_command()` (HTTP). Two problems for
MQTT: (a) a command must cross the esp-mqtt-task → poll_task boundary, so it
cannot hold cJSON pointers (the tree is freed when the event handler returns);
(b) HTTP delivers an array of commands per poll, MQTT delivers one command per
`cmd` message. Solve both by making a **fixed-size struct the common currency**:

```c
typedef struct { char command_id[40]; char type[24]; char action[24]; char url[512]; } dev_command_t;
typedef int (*ack_fn_t)(const char *command_id, bool ok);
void dispatch_command(const dev_command_t *cmd, ack_fn_t ack);   // pure of transport
```

- **HTTP path:** `commands_handle_body()` loops the array, parses each element
  into a `dev_command_t` (copied strings), and calls
  `dispatch_command(&cmd, cloud_ack_command)`.
- **MQTT path:** the event handler parses the single `cmd` message into a
  `dev_command_t`, enqueues it, and `poll_task` calls
  `dispatch_command(&cmd, mqtt_ack_thunk)`.

This keeps one dispatch implementation, guarantees **a command is acked on the
transport it arrived on** (so the cloud settles the credit against the right
device/command), and makes the parse-into-struct + dispatch decision pure and
host-testable. The per-type handling (`identify`, `reboot`,
`config-changed`/`refresh`, `firmware-update`, `trigger` → `on_trigger_show_qr`)
is unchanged; only the ack call becomes `ack(...)`.

### Notification-wake command path (single-threaded UI preserved)

The esp-mqtt client runs its own internal task. Today only `poll_task`
(`main/app_state.c`) renders; the UI is not thread-safe. So the esp-mqtt task
**never touches the UI** — it only parses and hands off:

```
esp-mqtt task (MQTT_EVENT_DATA on d/{id}/cmd):
  parse the message into a dev_command_t (copied strings, no cJSON kept)
  xQueueSend(cmd_queue, &dev_command)               // FreeRTOS queue = thread-safe
  xTaskNotify(poll_task_handle)

poll_task:
  idle wait becomes notification-interruptible (ulTaskNotifyTake / event-group
  wait with the QR-expiry timeout) → wakes immediately on notify
  xQueueReceive(cmd_queue, &cmd) → dispatch_command(&cmd, mqtt_ack_thunk)
    → on_trigger_show_qr → QR render → mqtt_publish_ack(command_id, ok)
```

`idle_wait_or_qr_expiry()` (currently a 500 ms-sliced sleep) is changed to wait
on a task notification with the same expiry timeout, so an MQTT command renders
without waiting for a 500 ms slice — yet all rendering stays on `poll_task`
(no UI race). A one-deep FreeRTOS queue carries the parsed command + its
transport tag between the two tasks.

## Connection lifecycle

- **Start when** the config (fresh or NVS-cached at boot) has an `mqtt` block
  with `enabled` (present + non-empty host) **and** `net_is_connected()`.
  On boot the device loads cached config (`cloud_config_load_cached`), so MQTT
  can connect from cache before the first fresh config fetch.
- **No `mqtt` block** (cloud MQTT off / old cloud) → don't start; behavior is
  byte-for-byte today's HTTP polling. **Non-breaking.**
- **Config change** to host/username → `mqtt_stop()` then `mqtt_start()`.
- **Reconnect** is handled internally by esp-mqtt.

### esp-mqtt config (Path A)

```
broker.address.hostname = cfg->mqtt.host
broker.address.port     = cfg->mqtt.port            // 8883
broker.address.transport = MQTT_TRANSPORT_OVER_SSL
broker.verification.crt_bundle_attach = esp_crt_bundle_attach   // existing bundle
credentials.client_id   = cfg->mqtt.client_id       // = deviceId
credentials.username    = cfg->mqtt.username         // = deviceId
credentials.authentication.password = appcfg_device_key()       // NVS device key
session.keepalive = 60                               // EMQX detects dead conn ~90 s
session.disable_clean_session = false                // clean session (see below)
buffer.size = 2048   buffer.out_size = 512           // modest (see Memory)
```

- **Subscribe** `d/{id}/cmd` QoS 1 in the `MQTT_EVENT_CONNECTED` handler
  (re-subscribe on every connect, since clean session).
- **Clean session** (not persistent): simpler, and robustness is already
  covered by two cloud safety nets — the heartbeat-triggered republish of stale
  pending commands, and the HTTP polling fallback. The broker does not queue
  pre-subscription messages, so:
- **Heartbeat on connect + every ~5 min.** Publishing `hb` immediately on
  connect makes the cloud republish any commands enqueued before the device
  subscribed. The periodic 5-min `hb` is driven from `poll_task`'s timing loop.

## Coexistence with `poll_task`

Each `poll_task` cycle checks `mqtt_is_connected()`:

- **MQTT connected:** skip `cloud_get_commands` (commands arrive via MQTT).
  Still run: local sleep/QR-expiry timing every cycle; `hb` publish (5 min);
  periodic OTA check (unchanged cadence); config fetch **only** when a
  `config-changed` command arrives (MQTT push) or as an occasional safety poll
  (~30 min). The idle wait is notification-interruptible. → Idle network
  traffic drops to ~zero.
- **MQTT not connected (or no mqtt config):** today's behavior — poll commands
  every ~12 s, **ack over HTTP**, poll config, OTA, error backoff — unchanged.
- **Ack-transport matching:** enforced by the injected `ack_fn` (MQTT-delivered
  → `mqtt_publish_ack`; HTTP-polled → `cloud_ack_command`).

## Config block parsing + deviceId

- Add to `device_config_t` (`components/devcfg/include/device_config.h`), a
  sibling of the `device` struct:
  ```c
  struct { bool enabled; char host[128]; int port; char client_id[64]; char username[64]; } mqtt;
  ```
- Parse a top-level `mqtt` object in `cfg_parse_json`
  (`components/devcfg/cfg_parse.c`) right after the `device` block, using the
  existing `copy_str` helper: `host`, `port` (default 8883), `clientId`,
  `username`; `enabled = (mqtt object present && host[0] != '\0')`. Pure cJSON,
  **host-tested** in `tools/cfg-harness`.
- **deviceId source:** `cfg->mqtt.username` (== `client_id`) IS the deviceId,
  delivered by the cloud in the config. No new NVS identity is stored — it flows
  from config each fetch (already persisted as part of `cfg_json`). The password
  is the existing NVS device key. Two credentials: **username from config,
  password from NVS.**

## Memory & risks

The main risk is internal DRAM pressure: a persistent MQTT-over-TLS session is a
second TLS session in internal RAM (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`), on
top of the SDIO mempool and the existing HTTPS client.

- **Modest esp-mqtt buffers** — `buffer.size = 2048` (command payloads are a few
  hundred bytes; 2 KB leaves room for a longer `payload.url`), `out_size = 512`
  (acks/hb are tiny).
- **Do NOT touch** the SDIO mempool / PSRAM settings (`sdkconfig.defaults`
  documents "do NOT re-enable MEMPOOL_PREFER_SPIRAM"); large buffers
  (config, decoded images, asset fetch) stay `MALLOC_CAP_SPIRAM`.
- **Heap logging:** log free internal DRAM (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`)
  around `mqtt_start` and periodically, to serial, so HIL confirms headroom.
- **Contingency (documented, not built):** if internal DRAM is too tight at HIL
  — tear down MQTT during large asset fetches, or shrink SDIO queues further.
  Measure first.
- **Concurrency:** the esp-mqtt task never calls UI/render; the only cross-task
  channel is the thread-safe FreeRTOS command queue + a task notification.

## Testing

**Host (`tools/cfg-harness`, `make test`) — pure logic:**
- `cfg_parse_json` parses the `mqtt` block: present / absent / partial →
  `enabled` correct, fields copied, `port` default.
- `mqtt_topics` builds `d/{id}/cmd|ack|hb` correctly.
- Dispatch decision with a mock `ack_fn`: a given command JSON invokes the right
  handler and calls the injected ack (extends the existing
  `command_trigger_show_qr_url` host test; light refactor to expose the
  dispatch decision without ESP deps).

**HIL (desk device b580, once the cloud is activated in prod — env + the console
webhooks configured):**
1. **Connect + presence:** serial shows `MQTT_EVENT_CONNECTED` + subscribe;
   cloud shows the device online instantly.
2. **Trigger latency:** trigger via the cloud API → measure API-202 → QR on
   screen. **Target < 1 s.**
3. **Ack + credit:** MQTT ack → cloud settles the credit (verify in admin).
4. **Heartbeat:** `hb` every 5 min → cloud `lastSeenAt`/version update.
5. **Fallback drill:** kill Wi-Fi / block the broker → device falls back to HTTP
   polling within ~15 s, triggers still render (via HTTP); restore → MQTT
   reconnects and command polling stops.
6. **Memory:** serial heap logs show stable internal DRAM headroom over time
   (no leak, no exhaustion).
7. **Old-cloud compat:** a config without the `mqtt` block → device stays on
   HTTP polling (no regression).

## Out of scope

- Persistent (non-clean) MQTT sessions and app-level LWT (cloud safety nets +
  HTTP fallback make them unnecessary).
- Changing the SDIO / PSRAM / mbedTLS memory configuration.
- Cloud-side changes (already built + merged in maratus-admin).
- The `MQTT_BROKER_HOST` value / EMQX console webhook setup (cloud/ops task).
