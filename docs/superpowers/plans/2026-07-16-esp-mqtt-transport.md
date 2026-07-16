# ESP-MQTT Device Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an MQTT client to the ESP32-P4 firmware so device commands arrive push-style (< 1 s) over MQTT, with HTTP polling kept as an automatic fallback.

**Architecture:** A new `components/mqtt/` wraps esp-mqtt (TLS via the existing cert bundle; `username = deviceId` from config, `password = device key` from NVS — Path A, no JWT). Command handling is refactored to a struct-based, transport-agnostic `dispatch_command(&cmd, ack_fn)`. The esp-mqtt task only parses each `d/{id}/cmd` message into a `dev_command_t`, enqueues it, and wakes `poll_task`; `poll_task` drains the queue and renders (single-threaded UI preserved). When MQTT is connected, `poll_task` skips the HTTP command poll; when it isn't, today's polling runs unchanged.

**Tech Stack:** ESP-IDF 5.5.4 (esp32p4), esp-mqtt (bundled), cJSON, FreeRTOS, mbedTLS cert bundle. Host tests via `tools/cfg-harness` (native `cc`, no ESP-IDF).

## Global Constraints

- **Non-breaking / additive:** with no `mqtt` block in `/api/device/config`, the device behaves byte-for-byte as today (HTTP polling). MQTT starts only when the config carries an `mqtt` block with a non-empty host.
- **Path A auth (NO JWT):** `client_id = username = cfg->mqtt.username` (= deviceId, from config); `password = appcfg_device_key()` (NVS device key). TLS via `esp_crt_bundle_attach`.
- **Topics (QoS 1):** subscribe `d/{deviceId}/cmd`; publish `d/{deviceId}/ack` (`{commandId,ok,result?}`) and `d/{deviceId}/hb` (`{version}`).
- **Single-threaded UI:** the esp-mqtt task MUST NOT call any `ui_*` / render / `app_state_*` function. It only parses + enqueues + wakes `poll_task`. All rendering stays on `poll_task`.
- **A command is acked on the transport it arrived on** (MQTT-delivered → `mqtt_ack`; HTTP-polled → `cloud_ack_command`).
- **Memory:** do NOT change SDIO/PSRAM/mbedTLS memory config (`sdkconfig.defaults` says "do NOT re-enable MEMPOOL_PREFER_SPIRAM"). Keep esp-mqtt buffers modest (`buffer.size=2048`, `out_size=512`). Log free internal DRAM around `mqtt_start`.
- **Pure logic is host-tested** in `tools/cfg-harness` (`make test` → prints `ALL TESTS PASSED`). esp-mqtt glue is build-verified (`idf.py build`) + HIL only.
- **Commit** after each task. This repo's convention: `idf.py build` per task (no on-target CI); host tests where the task adds pure logic.

---

## File Structure

**New:**
- `components/mqtt/mqtt_client.c` + `include/mqtt_client_ditto.h` — esp-mqtt glue: lifecycle, event handler, queue, ack/hb publish. (Header named `mqtt_client_ditto.h` to avoid clashing with IDF's `mqtt_client.h`.)
- `components/mqtt/mqtt_topics.c` + `include/mqtt_topics.h` — pure topic-string builders (host-tested).
- `components/mqtt/CMakeLists.txt`.

**Modified:**
- `components/devcfg/include/device_config.h` — add the `mqtt` sub-struct.
- `components/devcfg/cfg_parse.c` — parse the top-level `mqtt` object.
- `components/cloud/include/command_parse.h` + `command_parse.c` — add `dev_command_t` + `command_parse_into()` (pure).
- `components/cloud/commands.h` + `commands.c` — add `ack_fn_t` + `dispatch_command()`; refactor `commands_handle_body()` to use them.
- `components/cloud/CMakeLists.txt` — (no new REQUIRES; already has `json ui`).
- `main/app_state.c` — poll_task: capture task handle, drain MQTT queue, gate command poll on MQTT state, hb timer, notification-interruptible idle wait, start/stop MQTT from config, heap log.
- `main/CMakeLists.txt` — add `mqtt` to `REQUIRES`.
- `tools/cfg-harness/Makefile` + `test_cfg.c` — add the new pure sources + tests.

---

### Task 1: Config `mqtt` block — struct + parser (host-tested)

**Files:**
- Modify: `components/devcfg/include/device_config.h`, `components/devcfg/cfg_parse.c`
- Test: `tools/cfg-harness/test_cfg.c`, `tools/cfg-harness/Makefile`

**Interfaces:**
- Produces: `cfg->mqtt` (a sub-struct of `device_config_t`) — consumed by Task 5 (`mqtt_start`).

- [ ] **Step 1: Add the struct field**

In `components/devcfg/include/device_config.h`, inside `device_config_t`, add a field right after the `device` sub-struct (after its closing `} device;`) and before `cfg_screen_t screens[SCREEN_COUNT];`:

```c
    // MQTT transport (from the payload "mqtt" block). enabled=false when the
    // cloud omits it (MQTT off / old cloud) → device stays on HTTP polling.
    struct {
        bool enabled;
        char host[128];
        int  port;
        char client_id[64];
        char username[64];
    } mqtt;
```

- [ ] **Step 2: Write the failing host test**

In `tools/cfg-harness/test_cfg.c`, add a test function (place it just before `int main(void)`):

```c
static void test_mqtt_block(void) {
    // present + full → enabled, fields copied, port kept
    const char *j1 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"b.example.com\",\"port\":8883,\"clientId\":\"dev_9\",\"username\":\"dev_9\"}}";
    device_config_t c1; assert(cfg_parse_json(j1, &c1));
    assert(c1.mqtt.enabled);
    assert(strcmp(c1.mqtt.host, "b.example.com") == 0);
    assert(c1.mqtt.port == 8883);
    assert(strcmp(c1.mqtt.client_id, "dev_9") == 0);
    assert(strcmp(c1.mqtt.username, "dev_9") == 0);

    // absent → disabled
    const char *j2 = "{\"version\":\"1\"}";
    device_config_t c2; assert(cfg_parse_json(j2, &c2));
    assert(!c2.mqtt.enabled);

    // present but empty host → disabled
    const char *j3 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"\",\"username\":\"dev_9\"}}";
    device_config_t c3; assert(cfg_parse_json(j3, &c3));
    assert(!c3.mqtt.enabled);

    // host present, port omitted → default 8883
    const char *j4 = "{\"version\":\"1\",\"mqtt\":{\"host\":\"h\",\"clientId\":\"d\",\"username\":\"d\"}}";
    device_config_t c4; assert(cfg_parse_json(j4, &c4));
    assert(c4.mqtt.enabled && c4.mqtt.port == 8883);

    printf("test_mqtt_block OK\n");
}
```

Register it in `main()` — add `test_mqtt_block();` next to the other calls (e.g. after `test_device_defaults();`).

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd tools/cfg-harness && make test`
Expected: build succeeds but the run aborts on an assertion in `test_mqtt_block` (the `mqtt` field is zeroed by the initial `memset`, so `enabled` is false even for `j1`).

- [ ] **Step 4: Implement the parse**

In `components/devcfg/cfg_parse.c`, in `cfg_parse_json`, after the `device` block parse (right after the two `copy_str(cfg->device.settings_password_...)` calls) and before the `screens` loop, add:

```c
    cJSON *mq = cJSON_GetObjectItem(root, "mqtt");
    if (mq) {
        copy_str(cfg->mqtt.host, sizeof(cfg->mqtt.host), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "host")));
        copy_str(cfg->mqtt.client_id, sizeof(cfg->mqtt.client_id), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "clientId")));
        copy_str(cfg->mqtt.username, sizeof(cfg->mqtt.username), cJSON_GetStringValue(cJSON_GetObjectItem(mq, "username")));
        cJSON *port = cJSON_GetObjectItem(mq, "port");
        cfg->mqtt.port = cJSON_IsNumber(port) ? (int)port->valuedouble : 8883;
        cfg->mqtt.enabled = (cfg->mqtt.host[0] != '\0');
    }
```

(`memset` at the top of `cfg_parse_json` already zeroes `mqtt` when the block is absent, so `enabled` stays false — no else needed.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd tools/cfg-harness && make test`
Expected: PASS — output includes `test_mqtt_block OK` and ends with `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add components/devcfg/include/device_config.h components/devcfg/cfg_parse.c tools/cfg-harness/test_cfg.c
git commit -m "feat(cfg): parse mqtt block (host/port/clientId/username)"
```

---

### Task 2: `mqtt_topics` pure helper (host-tested)

**Files:**
- Create: `components/mqtt/mqtt_topics.c`, `components/mqtt/include/mqtt_topics.h`
- Test: `tools/cfg-harness/test_cfg.c`, `tools/cfg-harness/Makefile`

**Interfaces:**
- Produces: `mqtt_topic_cmd/ack/hb(char *out, size_t cap, const char *device_id)` — consumed by Task 4.

- [ ] **Step 1: Create the header**

Create `components/mqtt/include/mqtt_topics.h`:

```c
#pragma once
#include <stddef.h>

// Build the per-device MQTT topics into `out` (NUL-terminated, truncated to cap).
// device_id is the cloud deviceId (== config mqtt.username).
void mqtt_topic_cmd(char *out, size_t cap, const char *device_id);   // d/{id}/cmd  (subscribe)
void mqtt_topic_ack(char *out, size_t cap, const char *device_id);   // d/{id}/ack  (publish)
void mqtt_topic_hb(char *out, size_t cap, const char *device_id);    // d/{id}/hb   (publish)
```

- [ ] **Step 2: Write the failing host test**

In `tools/cfg-harness/test_cfg.c`, add `#include "mqtt_topics.h"` at the top, then a test before `int main`:

```c
static void test_mqtt_topics(void) {
    char t[64];
    mqtt_topic_cmd(t, sizeof(t), "dev_9"); assert(strcmp(t, "d/dev_9/cmd") == 0);
    mqtt_topic_ack(t, sizeof(t), "dev_9"); assert(strcmp(t, "d/dev_9/ack") == 0);
    mqtt_topic_hb(t, sizeof(t), "dev_9");  assert(strcmp(t, "d/dev_9/hb") == 0);
    printf("test_mqtt_topics OK\n");
}
```

Register `test_mqtt_topics();` in `main()`.

In `tools/cfg-harness/Makefile`: add `-I../../components/mqtt/include` to `CFLAGS`, and add `../../components/mqtt/mqtt_topics.c` to `SRCS`.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd tools/cfg-harness && make test`
Expected: FAIL to build — `mqtt_topics.c: No such file` (source not created yet).

- [ ] **Step 4: Implement the helper**

Create `components/mqtt/mqtt_topics.c`:

```c
#include "mqtt_topics.h"
#include <stdio.h>

void mqtt_topic_cmd(char *out, size_t cap, const char *device_id) { snprintf(out, cap, "d/%s/cmd", device_id ? device_id : ""); }
void mqtt_topic_ack(char *out, size_t cap, const char *device_id) { snprintf(out, cap, "d/%s/ack", device_id ? device_id : ""); }
void mqtt_topic_hb(char *out, size_t cap, const char *device_id)  { snprintf(out, cap, "d/%s/hb",  device_id ? device_id : ""); }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd tools/cfg-harness && make test`
Expected: PASS — `test_mqtt_topics OK` + `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add components/mqtt/mqtt_topics.c components/mqtt/include/mqtt_topics.h tools/cfg-harness/Makefile tools/cfg-harness/test_cfg.c
git commit -m "feat(mqtt): pure per-device topic builders"
```

---

### Task 3: Struct-based transport-agnostic command dispatch (host-tested)

**Files:**
- Modify: `components/cloud/include/command_parse.h`, `components/cloud/command_parse.c`, `components/cloud/commands.h`, `components/cloud/commands.c`
- Test: `tools/cfg-harness/test_cfg.c`

**Interfaces:**
- Consumes: existing `command_trigger_show_qr_url` (`command_parse.c`), the cloud handler getters (`cloud_trigger_handler`, `cloud_config_changed_handler`, `cloud_firmware_update_handler`, `cloud_ack_command`) from `cloud.h`.
- Produces:
  - `typedef struct { char command_id[40]; char type[24]; char action[24]; char url[512]; } dev_command_t;` (in `command_parse.h`)
  - `bool command_parse_into(const cJSON *cmd, dev_command_t *out);` (pure; in `command_parse.c`) — consumed by Task 4.
  - `typedef int (*ack_fn_t)(const char *command_id, bool ok);` + `void dispatch_command(const dev_command_t *cmd, ack_fn_t ack);` (in `commands.h`) — consumed by Task 5.

- [ ] **Step 1: Write the failing host test for `command_parse_into`**

In `tools/cfg-harness/test_cfg.c`, add a test before `int main`:

```c
static void test_command_parse_into(void) {
    dev_command_t c;
    cJSON *ok = cJSON_Parse("{\"id\":\"cmd_1\",\"type\":\"trigger\",\"action\":\"show_qr\",\"payload\":{\"url\":\"https://x/y\"}}");
    assert(command_parse_into(ok, &c));
    assert(strcmp(c.command_id, "cmd_1") == 0);
    assert(strcmp(c.type, "trigger") == 0);
    assert(strcmp(c.action, "show_qr") == 0);
    assert(strcmp(c.url, "https://x/y") == 0);
    cJSON_Delete(ok);

    // non-trigger: type/id copied, url empty
    cJSON *rb = cJSON_Parse("{\"id\":\"cmd_2\",\"type\":\"reboot\"}");
    assert(command_parse_into(rb, &c));
    assert(strcmp(c.type, "reboot") == 0 && c.url[0] == '\0');
    cJSON_Delete(rb);

    // missing id or type → false
    cJSON *bad = cJSON_Parse("{\"type\":\"reboot\"}");
    assert(!command_parse_into(bad, &c));
    cJSON_Delete(bad);

    printf("test_command_parse_into OK\n");
}
```

Register `test_command_parse_into();` in `main()`. (`command_parse.c` is already in the harness `SRCS`; `cJSON.h` already included.)

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd tools/cfg-harness && make test`
Expected: FAIL to build — `command_parse_into` undeclared.

- [ ] **Step 3: Add `dev_command_t` + `command_parse_into`**

In `components/cloud/include/command_parse.h`, append:

```c
#include <stdbool.h>

// A command copied out of the transient cJSON tree so it can cross a task
// boundary and be dispatched transport-independently.
typedef struct {
    char command_id[40];
    char type[24];
    char action[24];
    char url[512];   // payload.url for action==show_qr; "" otherwise
} dev_command_t;

// Parse one command object into `out`. Returns false if id or type is missing.
bool command_parse_into(const cJSON *cmd, dev_command_t *out);
```

In `components/cloud/command_parse.c`, add (keep the existing `command_trigger_show_qr_url`):

```c
#include <stdio.h>

static void copy_field(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

bool command_parse_into(const cJSON *cmd, dev_command_t *out) {
    if (!cmd || !out) return false;
    const char *id   = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "id"));
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "type"));
    if (!id || !type) return false;
    copy_field(out->command_id, sizeof(out->command_id), id);
    copy_field(out->type, sizeof(out->type), type);
    copy_field(out->action, sizeof(out->action), cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)cmd, "action")));
    const char *url = command_trigger_show_qr_url(cmd);   // NULL unless show_qr + payload.url
    copy_field(out->url, sizeof(out->url), url);
    return true;
}
```

- [ ] **Step 4: Run the host test to verify it passes**

Run: `cd tools/cfg-harness && make test`
Expected: PASS — `test_command_parse_into OK` + `ALL TESTS PASSED`.

- [ ] **Step 5: Add `dispatch_command` and refactor `commands_handle_body`**

In `components/cloud/commands.h`, append:

```c
#include "command_parse.h"

// Ack transport: HTTP passes cloud_ack_command; MQTT passes mqtt_ack.
typedef int (*ack_fn_t)(const char *command_id, bool ok);

// Dispatch one already-parsed command, acking via `ack`. Transport-independent.
void dispatch_command(const dev_command_t *cmd, ack_fn_t ack);
```

In `components/cloud/commands.c`, replace the body of `commands_handle_body` and add `dispatch_command`. The per-type logic moves verbatim into `dispatch_command`, calling `ack(...)` instead of `cloud_ack_command(...)` and reading fields from the struct:

```c
void dispatch_command(const dev_command_t *cmd, ack_fn_t ack)
{
    const char *id = cmd->command_id;
    ESP_LOGI(TAG, "command %s type=%s", id, cmd->type);

    if (strcmp(cmd->type, "identify") == 0) {
        do_identify();
        ack(id, true);
    } else if (strcmp(cmd->type, "reboot") == 0) {
        ack(id, true);
        ESP_LOGI(TAG, "rebooting");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    } else if (strcmp(cmd->type, "config-changed") == 0 || strcmp(cmd->type, "refresh") == 0) {
        void (*cb)(void) = cloud_config_changed_handler();
        if (cb) cb();
        ack(id, true);
    } else if (strcmp(cmd->type, "firmware-update") == 0) {
        ack(id, true);
        void (*fcb)(void) = cloud_firmware_update_handler();
        if (fcb) fcb();
    } else if (strcmp(cmd->type, "trigger") == 0) {
        void (*tcb)(const char *) = cloud_trigger_handler();
        if (cmd->url[0] && tcb) { tcb(cmd->url); ack(id, true); }
        else { ack(id, false); }
    } else {
        ack(id, true);
    }
}

void commands_handle_body(const char *json_body)
{
    if (!json_body || !json_body[0]) return;
    cJSON *root = cJSON_Parse(json_body);
    if (!root) { ESP_LOGW(TAG, "bad JSON"); return; }
    cJSON *cmds = cJSON_GetObjectItem(root, "commands");
    int n = cJSON_IsArray(cmds) ? cJSON_GetArraySize(cmds) : 0;
    for (int i = 0; i < n; i++) {
        dev_command_t c;
        if (command_parse_into(cJSON_GetArrayItem(cmds, i), &c))
            dispatch_command(&c, cloud_ack_command);
    }
    cJSON_Delete(root);
}
```

`commands.c` already includes `commands.h`; `command_parse.h` is now pulled in via `commands.h`. `cloud_ack_command` is `int cloud_ack_command(const char *command_id, bool ok)` (confirmed in `cloud.h:19`), which matches `ack_fn_t` exactly — pass it directly, no wrapper needed.

- [ ] **Step 6: Build (on-target compile check)**

Run: `idf.py build`
Expected: builds clean. The HTTP command flow is now routed through `dispatch_command` with identical behavior.

- [ ] **Step 7: Commit**

```bash
git add components/cloud/include/command_parse.h components/cloud/command_parse.c components/cloud/commands.h components/cloud/commands.c tools/cfg-harness/test_cfg.c
git commit -m "refactor(cloud): struct-based transport-agnostic dispatch_command"
```

---

### Task 4: `components/mqtt/` — esp-mqtt client glue

**Files:**
- Create: `components/mqtt/mqtt_client.c`, `components/mqtt/include/mqtt_client_ditto.h`, `components/mqtt/CMakeLists.txt`

**Interfaces:**
- Consumes: `dev_command_t` + `command_parse_into` (`command_parse.h`, Task 3); `mqtt_topic_*` (Task 2); `appcfg_device_key()`; `device_config_t` (`device_config.h`).
- Produces (consumed by Task 5):
  - `bool mqtt_start(const device_config_t *cfg);`
  - `void mqtt_stop(void);`
  - `bool mqtt_is_connected(void);`
  - `bool mqtt_poll_command(dev_command_t *out);` — non-blocking dequeue; true if one was returned.
  - `void mqtt_set_wake_cb(void (*cb)(void));` — called (from the esp-mqtt task) when a command was enqueued.
  - `int mqtt_ack(const char *command_id, bool ok);` — publish to `d/{id}/ack`; `ack_fn_t`-compatible.
  - `void mqtt_publish_hb(const char *version);` — publish to `d/{id}/hb`.

- [ ] **Step 1: Create the CMakeLists**

Create `components/mqtt/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "mqtt_client.c" "mqtt_topics.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp-tls esp_event appcfg devcfg json cloud
)
```

- [ ] **Step 2: Create the header**

Create `components/mqtt/include/mqtt_client_ditto.h`:

```c
#pragma once
#include <stdbool.h>
#include "device_config.h"
#include "command_parse.h"   // dev_command_t

// Start the MQTT client from a config that has an enabled mqtt block. Safe to
// call when already running (no-op if the same host/username). Returns false if
// cfg->mqtt.enabled is false or the device key is missing.
bool mqtt_start(const device_config_t *cfg);
void mqtt_stop(void);
bool mqtt_is_connected(void);

// Non-blocking: pop the next MQTT-delivered command into `out`. True if one was
// returned. Drain in a while-loop from poll_task after a wake.
bool mqtt_poll_command(dev_command_t *out);

// Registered by main; invoked (on the esp-mqtt task) after a command is queued,
// so poll_task can be woken to drain it. Keep the cb tiny (a task notify).
void mqtt_set_wake_cb(void (*cb)(void));

// Publish helpers. mqtt_ack matches ack_fn_t. No-ops when not connected.
int  mqtt_ack(const char *command_id, bool ok);
void mqtt_publish_hb(const char *version);
```

- [ ] **Step 3: Implement `mqtt_client.c`**

Create `components/mqtt/mqtt_client.c`:

```c
#include "mqtt_client_ditto.h"
#include "mqtt_topics.h"
#include "mqtt_client.h"          // esp-mqtt
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "appcfg.h"
#include <string.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static QueueHandle_t s_cmd_q;                 // dev_command_t, depth 4
static void (*s_wake_cb)(void);
static char s_device_id[64];                  // = username/client_id from config
static char s_host[128];                      // remember to detect config changes

static void on_cmd_message(const char *data, int len)
{
    cJSON *cmd = cJSON_ParseWithLength(data, len);
    if (!cmd) { ESP_LOGW(TAG, "bad cmd json"); return; }
    dev_command_t dc;
    if (command_parse_into(cmd, &dc)) {
        if (xQueueSend(s_cmd_q, &dc, 0) == pdTRUE && s_wake_cb) s_wake_cb();
        else if (s_cmd_q == NULL) { /* not started */ }
    }
    cJSON_Delete(cmd);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        char topic[80];
        mqtt_topic_cmd(topic, sizeof(topic), s_device_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);   // QoS 1, re-sub each connect
        ESP_LOGI(TAG, "connected, subscribed %s", topic);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
        // Only the cmd topic is subscribed, so any data is a command.
        on_cmd_message(e->data, e->data_len);
        break;
    default: break;
    }
}

bool mqtt_start(const device_config_t *cfg)
{
    if (!cfg || !cfg->mqtt.enabled) return false;
    const char *key = appcfg_device_key();
    if (!key || !key[0]) { ESP_LOGW(TAG, "no device key; MQTT not started"); return false; }

    // Already running for the same broker/identity → no-op.
    if (s_client && strcmp(s_host, cfg->mqtt.host) == 0 &&
        strcmp(s_device_id, cfg->mqtt.username) == 0) return true;
    if (s_client) mqtt_stop();

    snprintf(s_device_id, sizeof(s_device_id), "%s", cfg->mqtt.username);
    snprintf(s_host, sizeof(s_host), "%s", cfg->mqtt.host);
    if (!s_cmd_q) s_cmd_q = xQueueCreate(4, sizeof(dev_command_t));

    ESP_LOGI(TAG, "starting; free internal DRAM before=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_mqtt_client_config_t c = {
        .broker.address.hostname = cfg->mqtt.host,
        .broker.address.port = cfg->mqtt.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = cfg->mqtt.client_id,
        .credentials.username = cfg->mqtt.username,
        .credentials.authentication.password = key,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
        .buffer.size = 2048,
        .buffer.out_size = 512,
    };
    s_client = esp_mqtt_client_init(&c);
    if (!s_client) { ESP_LOGE(TAG, "init failed"); return false; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "started; free internal DRAM after=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

void mqtt_stop(void)
{
    if (!s_client) return;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
    s_host[0] = '\0'; s_device_id[0] = '\0';
}

bool mqtt_is_connected(void) { return s_connected; }

bool mqtt_poll_command(dev_command_t *out)
{
    if (!s_cmd_q) return false;
    return xQueueReceive(s_cmd_q, out, 0) == pdTRUE;
}

void mqtt_set_wake_cb(void (*cb)(void)) { s_wake_cb = cb; }

int mqtt_ack(const char *command_id, bool ok)
{
    if (!s_client || !s_connected) return -1;
    char topic[80];
    mqtt_topic_ack(topic, sizeof(topic), s_device_id);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "commandId", command_id);
    cJSON_AddBoolToObject(j, "ok", ok);
    char *body = cJSON_PrintUnformatted(j);
    int r = esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);   // QoS 1
    cJSON_free(body); cJSON_Delete(j);
    return r < 0 ? -1 : 0;
}

void mqtt_publish_hb(const char *version)
{
    if (!s_client || !s_connected) return;
    char topic[80];
    mqtt_topic_hb(topic, sizeof(topic), s_device_id);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "version", version ? version : "");
    char *body = cJSON_PrintUnformatted(j);
    esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);
    cJSON_free(body); cJSON_Delete(j);
}
```

- [ ] **Step 4: Build**

Run: `idf.py build`
Expected: builds clean. (If the linker complains `mqtt_topics.c` is compiled twice — it is listed in this component AND the harness Makefile; the harness is a separate native build, so no conflict. Within IDF, `mqtt_topics.c` belongs only to this component.)

- [ ] **Step 5: Commit**

```bash
git add components/mqtt/
git commit -m "feat(mqtt): esp-mqtt client glue (connect, subscribe, ack, hb, queue)"
```

---

### Task 5: poll_task integration + lifecycle wiring

**Files:**
- Modify: `main/app_state.c`, `main/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Task 4 (`mqtt_start`, `mqtt_stop`, `mqtt_is_connected`, `mqtt_poll_command`, `mqtt_set_wake_cb`, `mqtt_ack`, `mqtt_publish_hb`); `dispatch_command` (Task 3); `appcfg_fw_version()`.

- [ ] **Step 1: Add `mqtt` to main's REQUIRES**

In `main/CMakeLists.txt`, add `mqtt` to the `REQUIRES` list.

- [ ] **Step 2: Add includes, the task handle, the wake callback, and hb timing**

In `main/app_state.c`, add near the other includes:

```c
#include "mqtt_client_ditto.h"
#include "commands.h"        // dispatch_command, ack_fn_t
#include "appcfg.h"          // appcfg_fw_version
```

Add file-scope state near the other statics (after `s_ota_poll_ctr`):

```c
static TaskHandle_t s_poll_handle;
#define MQTT_IDLE_MS 30000
#define HB_EVERY_MS  300000          // 5 min
static int64_t s_last_hb_ms;
static bool s_was_mqtt_up;

// Called from the esp-mqtt task: wake poll_task so it drains mqtt_poll_command().
static void on_mqtt_wake(void) { if (s_poll_handle) xTaskNotifyGive(s_poll_handle); }
```

- [ ] **Step 3: Make `idle_wait_or_qr_expiry` notification-interruptible**

In `idle_wait_or_qr_expiry`, replace the trailing `if (waited < total_ms) vTaskDelay(pdMS_TO_TICKS(slice));` with a notification-take that returns early when an MQTT command was queued:

```c
        if (waited < total_ms) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(slice)) > 0) return;  // MQTT command queued → drain now
        }
```

(The 500 ms slice cadence for sleep/countdown is preserved; a notify just returns early. Rendering still happens only on `poll_task`.)

- [ ] **Step 4: Drain the queue + gate the command poll in `poll_task`**

In `poll_task`, replace the command-poll section. Find:

```c
        int status = cloud_get_commands(body, sizeof(body));
        if (status == 200) {
            ui_set_online(true);
```

and change the status acquisition + command handling so MQTT, when connected, supplies liveness without an HTTP poll and delivers commands via the queue:

```c
        // Drain any MQTT-delivered commands first (instant path). Harmless when empty.
        { dev_command_t mc; while (mqtt_poll_command(&mc)) dispatch_command(&mc, mqtt_ack); }

        bool mqtt_up = mqtt_is_connected();
        int status = mqtt_up ? 200 : cloud_get_commands(body, sizeof(body));
        if (status == 200) {
            ui_set_online(true);
```

Then, inside the `if (status == 200)` block, guard the HTTP command handling so it only runs on the HTTP path. Find `commands_handle_body(body);` and replace with:

```c
            if (!mqtt_up) commands_handle_body(body);   // MQTT commands arrived via the queue above
```

- [ ] **Step 5: Publish heartbeat (on connect + every 5 min) and use the longer idle interval when MQTT is up**

Still inside the `if (status == 200)` block, just before `backoff = POLL_BACKOFF_MIN;`, add the heartbeat logic:

```c
            if (mqtt_up) {
                if (!s_was_mqtt_up) { mqtt_publish_hb(appcfg_fw_version()); s_last_hb_ms = now_ms(); }  // immediate hb on connect → cloud republishes pending
                else if (now_ms() - s_last_hb_ms >= HB_EVERY_MS) { mqtt_publish_hb(appcfg_fw_version()); s_last_hb_ms = now_ms(); }
            }
            s_was_mqtt_up = mqtt_up;
```

And change the final `idle_wait_or_qr_expiry(POLL_IDLE_MS);` (the one at the end of the 200 branch) to:

```c
            idle_wait_or_qr_expiry(mqtt_up ? MQTT_IDLE_MS : POLL_IDLE_MS);
```

- [ ] **Step 6: Start/refresh MQTT from config, capture the task handle, register the wake cb**

In `app_state_run`, capture the task handle at creation — change:

```c
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, NULL);
```
to:
```c
    mqtt_set_wake_cb(on_mqtt_wake);
    if (s_cfg_buf[s_cfg_live]->valid && s_cfg_buf[s_cfg_live]->mqtt.enabled && net_is_connected())
        mqtt_start(s_cfg_buf[s_cfg_live]);   // start from cached config on boot (fast connect)
    xTaskCreate(poll_task, "ditto_poll", 8192, NULL, 5, &s_poll_handle);
```

Then, in `poll_task`, after a fresh config is published (inside the `if (cs == 200 && s_cfg_buf[next]->valid)` block that ends with `s_cfg_live = next;`), keep MQTT in sync with the new config — add right after `s_cfg_live = next;`:

```c
                    if (s_cfg_buf[s_cfg_live]->mqtt.enabled) mqtt_start(s_cfg_buf[s_cfg_live]);
                    else mqtt_stop();
```

(`mqtt_start` is a no-op if already running for the same host/username; it restarts on a change; `mqtt_stop` turns MQTT off if the cloud dropped the block.)

- [ ] **Step 7: Build**

Run: `idf.py build`
Expected: builds clean.

- [ ] **Step 8: Full host test + commit**

Run: `cd tools/cfg-harness && make test`
Expected: `ALL TESTS PASSED` (no regressions).

```bash
git add main/app_state.c main/CMakeLists.txt
git commit -m "feat(mqtt): poll_task MQTT integration (drain, gate, hb, lifecycle)"
```

---

### Task 6: On-device build, flash & HIL validation

**Files:** none (validation).

**Prerequisite:** the cloud must be activated for the test org — EMQX env set on the deployment serving this device's `/api/device/config`, and the three EMQX console webhooks configured (see `ditto-admin/docs/runbooks/emqx-setup.md`). The desk device is **b580** (Starbucks test org). b580 may need a re-claim so the cloud provisions its MQTT credential (device key = MQTT password).

- [ ] **Step 1: Build + flash**

Run: `idf.py build && idf.py -p <PORT> flash monitor`
Expected: builds, flashes, boots. Serial shows the device coming online.

- [ ] **Step 2: Connect + presence**

Watch the serial log for `mqtt: connected, subscribed d/<deviceId>/cmd` and the `free internal DRAM before/after` lines. In the admin, the device shows **online** (via the presence webhook) within a couple seconds.
Record the internal-DRAM headroom (after-start value) — this is the memory check.

- [ ] **Step 3: Trigger latency (< 1 s)**

Send a trigger via the cloud API (`POST /api/v1/devices/{deviceId}/trigger`, `{action:"show_qr", payload:{url}}`). Measure wall-clock from the API `202` to the QR appearing on the screen. **Target < 1 s.** Confirm the serial log shows the command arriving on the MQTT path (not an HTTP poll).

- [ ] **Step 4: Ack + credit settle**

After the QR renders, confirm in the admin that the trigger's credit **settled** (the Starbucks balance dropped by 1 / the hold cleared). This proves `mqtt_ack` → cloud ack webhook → credit settlement end-to-end.

- [ ] **Step 5: Heartbeat**

Leave the device idle ~6 min. Confirm the admin's `lastSeenAt` keeps updating (the 5-min `hb`) and the firmware version is recorded.

- [ ] **Step 6: Fallback drill**

Block the broker (e.g. disable Wi-Fi briefly, or firewall the broker host). Confirm the serial log shows `mqtt: disconnected`, the device resumes HTTP command polling within ~15 s, and a trigger still renders (via HTTP, acked via HTTP). Restore connectivity → `mqtt: connected` again and HTTP command polling stops (serial shows no more `cloud_get_commands` when MQTT up).

- [ ] **Step 7: Memory soak + concurrent-TLS peak (the spec's biggest risk)**

Leave the device connected ~30 min under occasional triggers. Confirm the periodic internal-DRAM logs stay stable (no downward drift / leak) and the device never restarts from an allocation failure.

Then explicitly exercise the two moments where a **third** concurrent internal-DRAM TLS session stacks on top of the persistent MQTT session + the base HTTPS client — this is the worst-case memory peak the spec flagged and it is NOT covered by idle soak:
- **Trigger during asset prefetch:** push a config change that adds/changes a branding asset (so `prefetch_and_evict_assets` runs an HTTPS asset fetch) while MQTT is connected, and fire a trigger in the same window. Log `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` at the peak.
- **Firmware-update while MQTT connected:** issue a `firmware-update` command (OTA download via `esp_https_ota`) with MQTT connected. Log peak internal-DRAM during the download.

Record the two peak free-internal-DRAM values. If either approaches exhaustion (device instability / alloc failures), apply the spec's documented contingency — `mqtt_stop()` around the large HTTPS transfer, then `mqtt_start()` after — as a fast-follow. Measure before mitigating (per the spec).

- [ ] **Step 8: Old-cloud compatibility**

Point the device at a config without an `mqtt` block (or confirm behavior when the cloud omits it): the device stays on HTTP polling, no MQTT start, no regression.

- [ ] **Step 9: Record results + commit any fixes**

Note the measured trigger latency and DRAM headroom in the HIL notes. Commit any fixes found during HIL with `fix(mqtt): ...` messages.

---

## Self-Review Notes

- **Spec coverage:** config `mqtt` parse (Task 1), topics (Task 2), transport-agnostic struct dispatch + ack-transport matching (Task 3), esp-mqtt client incl. TLS/auth/subscribe/ack/hb/queue (Task 4), notification-wake + poll_task gating + lifecycle + hb-on-connect + heap logging (Task 5), full HIL incl. latency/credit/fallback/memory/old-cloud (Task 6). Clean session + no app LWT are honored by omission (nothing sets an LWT; `disable_clean_session=false`).
- **Single-threaded UI:** the esp-mqtt event handler only parses + `xQueueSend` + `s_wake_cb`; all `dispatch_command`/render run on `poll_task`. No `ui_*` call on the mqtt task.
- **Non-breaking:** with `mqtt.enabled` false, `mqtt_start` returns early, `mqtt_is_connected()` is false, `poll_task` runs today's HTTP path unchanged.
- **Type consistency:** `dev_command_t`, `command_parse_into`, `ack_fn_t`, `dispatch_command`, and the `mqtt_*` signatures are identical across Tasks 3–5. `mqtt_ack` matches `ack_fn_t` (`int(const char*, bool)`); Task 3 Step 5 notes the `cloud_ack_command` return-type wrap if needed.
- **Memory:** no SDIO/PSRAM/mbedTLS config change; modest esp-mqtt buffers; internal-DRAM logged at start + soak-tested at HIL.
- **Host-testable pure logic** (config parse, topics, command_parse_into) is covered in `tools/cfg-harness`; esp-mqtt glue + poll_task are build- + HIL-verified (the repo has no on-target test framework, consistent with prior firmware milestones).
