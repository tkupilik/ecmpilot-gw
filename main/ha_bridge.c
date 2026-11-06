// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "ha_bridge.h"

#include "ha_settings.h"
#include "ha_telemetry.h"
#include "udp_protocol.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if !CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED || !CONFIG_MQTT_USE_CORE_0
#error "Pin the MQTT client to CPU0"
#endif

static esp_mqtt_client_handle_t client;
static atomic_bool connected, restart_discovery;
static atomic_int last_error;
static TaskHandle_t publisher_task;
static char node[40], serial[13], state_topic[160], status_topic[160];
static char diagnostics_topic[176];
static char command_topic[176], response_topic[176], command_info_topic[176];
typedef struct {
  uint64_t received_at;
  uint32_t connection;
  size_t length;
  char payload[GW_MQTT_MAX_BYTES + 1];
} ha_request_t;
typedef struct { char payload[GW_UDP_MAX_BYTES + 1]; } ha_response_t;
static _Atomic(QueueHandle_t) request_queue, response_queue;
static atomic_uint connection_generation;
bool gw_ha_connected(void) { return atomic_load(&connected); }
esp_err_t gw_ha_last_error(void) { return atomic_load(&last_error); }

/*****************************************************************************/

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
  (void)arg; (void)base;
  esp_mqtt_event_handle_t message = data;
  if (event == MQTT_EVENT_CONNECTED) {
    atomic_fetch_add(&connection_generation, 1);
    esp_mqtt_client_subscribe(message->client, command_topic, 1);
    atomic_store(&restart_discovery, true);
    atomic_store(&connected, true);
    atomic_store(&last_error, ESP_OK);
    ESP_LOGI("ha", "MQTT connected");
  } else if (event == MQTT_EVENT_DISCONNECTED) {
    atomic_store(&connected, false);
    ESP_LOGI("ha", "MQTT disconnected");
  } else if (event == MQTT_EVENT_ERROR) {
    atomic_store(&last_error, ESP_FAIL);
    ESP_LOGW("ha", "MQTT connection/transport error");
  } else if (event == MQTT_EVENT_DATA) {
    // ESP-MQTT can deliver a publication in fragments; only a complete,
    // non-retained publication on our exact command topic enters the queue.
    static ha_request_t incoming;
    static size_t expected, received;
    if (message->current_data_offset == 0) {
      expected = received = 0;
      if (message->retain || message->topic_len != (int)strlen(command_topic) ||
          memcmp(message->topic, command_topic, (size_t)message->topic_len) ||
          message->total_data_len <= 0 || message->total_data_len > GW_MQTT_MAX_BYTES) return;
      expected = (size_t)message->total_data_len;
      incoming.connection = atomic_load(&connection_generation);
      incoming.received_at = (uint64_t)esp_timer_get_time() / 1000;
    }
    if (!expected || message->current_data_offset < 0 || message->data_len < 0 ||
        (size_t)message->current_data_offset != received || (size_t)message->total_data_len != expected ||
        (size_t)message->data_len > expected - received) { expected = received = 0; return; }
    if (message->data_len) memcpy(incoming.payload + received, message->data, (size_t)message->data_len);
    received += (size_t)message->data_len;
    if (received == expected) {
      incoming.length = received; incoming.payload[received] = 0;
      if (xQueueSend(atomic_load(&request_queue), &incoming, 0) != pdPASS)
        ESP_LOGW("ha", "Command queue full; request dropped without execution");
      expected = received = 0;
    }
  }
}

void gw_ha_process_commands(uint64_t now)
{
  QueueHandle_t requests = atomic_load(&request_queue), replies = atomic_load(&response_queue);
  if (!requests || !replies) return;
  // Keep the command codec/cache single-owner, shared with phone UDP requests.
  // Reserve reply space before accepting work or consuming a terminal receipt.
  if (!uxQueueSpacesAvailable(replies)) return;
  ha_request_t request;
  ha_response_t response;
  if (xQueueReceive(requests, &request, 0) == pdPASS) {
    if (!atomic_load(&connected) || request.connection != atomic_load(&connection_generation) ||
        now - request.received_at > 5000) {
      ESP_LOGW("ha", "Expired/disconnected MQTT command dropped without execution");
    } else if (gw_udp_protocol_mqtt_handle(request.payload, request.length, false, now, response.payload)) {
      xQueueSend(replies, &response, 0);
    }
  }
  if (uxQueueSpacesAvailable(replies) && gw_udp_protocol_mqtt_poll(now, response.payload))
    xQueueSend(replies, &response, 0);
}

static esp_err_t start_client(const gw_ha_settings_t *s)
{
  char uri[160];
  snprintf(uri, sizeof(uri), "mqtt://%s:%lu", s->host, (unsigned long)s->port);
  esp_mqtt_client_config_t cfg = {
    .broker.address.uri = uri,
    .credentials.username = s->username[0] ? s->username : NULL,
    .credentials.authentication.password = s->password[0] ? s->password : NULL,
    .session.last_will.topic = status_topic,
    .session.last_will.msg = "offline",
    .session.last_will.qos = 1,
    .session.last_will.retain = true,
    .network.timeout_ms = 1000,
    .task.priority = 3,
    .task.stack_size = 6144,
    .buffer.size = 1024,
    .outbox.limit = 8192,
  };
  client = esp_mqtt_client_init(&cfg);
  if (!client) return ESP_ERR_NO_MEM;
  esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
  if (err == ESP_OK) err = esp_mqtt_client_start(client);
  if (err != ESP_OK) { esp_mqtt_client_destroy(client); client = NULL; }
  return err;
}

static void publisher(void *arg)
{
  (void)arg;
  configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
  size_t discovery = 0, retired = 0;
  bool announced = false, cleared_state = false, command_info_sent = false;
  uint64_t next_state = 0;
  uint64_t next_heartbeat = 0, diagnostics_at = 0, diagnostics_retry = 0;
  bool diagnostics_sent = false;
  char *last_state = NULL;
  gw_ha_settings_t settings;
  uint32_t applied = 0;
  uint64_t retry_at = 0;
  for (;;) {
    gw_ha_settings_get(&settings);
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
    if (settings.revision != applied) {
      // Only this CPU0 task owns the client lifecycle, never the HTTP handler.
      if (client) {
        if (atomic_load(&connected))
          esp_mqtt_client_publish(client, status_topic, "offline", 0, 1, 1);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client); client = NULL;
      }
      atomic_store(&connected, false);
      cJSON_free(last_state); last_state = NULL;
      atomic_store(&last_error, ESP_OK);
      applied = settings.revision; retry_at = 0;
      ESP_LOGI("ha", "MQTT configuration applied: %s", settings.enabled ? "enabled" : "disabled");
    }
    if (settings.enabled && !client && now >= retry_at) {
      esp_err_t err = start_client(&settings);
      atomic_store(&last_error, err);
      if (err != ESP_OK) ESP_LOGW("ha", "MQTT start: %s", esp_err_to_name(err));
      retry_at = now + 5000;
    }
    if (atomic_load(&connected)) {
      if (atomic_exchange(&restart_discovery, false)) {
        discovery = retired = 0; announced = cleared_state = false; next_state = 0;
        command_info_sent = false;
        next_heartbeat = diagnostics_retry = 0; diagnostics_sent = false;
        cJSON_free(last_state); last_state = NULL;
      }
      if (!command_info_sent) {
        char payload[320];
        snprintf(payload, sizeof(payload),
            "{\"v\":1,\"boot\":\"%s\",\"operations\":[\"vehicle.refresh\",\"command.get\"],\"request_topic\":\"%s\"}",
            gw_udp_protocol_boot_id(), command_topic);
        command_info_sent = esp_mqtt_client_enqueue(client, command_info_topic, payload, 0, 1, 1, true) >= 0;
      }
      ha_response_t response;
      if (xQueuePeek(atomic_load(&response_queue), &response, 0) == pdPASS &&
          esp_mqtt_client_enqueue(client, response_topic, response.payload, 0, 1, 0, true) >= 0)
        xQueueReceive(atomic_load(&response_queue), &response, 0);
      if (!cleared_state) {
        // An empty retained deletion is also delivered to every existing HA
        // subscriber and breaks legacy JSON templates. Retain null values
        // instead: a later HA subscription cannot restore stale vehicle data.
        char *payload = gw_ha_unavailable_json();
        cleared_state = payload && esp_mqtt_client_enqueue(client, state_topic, payload, 0, 1, 1, true) >= 0;
        cJSON_free(payload);
      }
      if (cleared_state && retired < gw_ha_retired_count()) {
        // One tombstone per tick; retry the same topic if the outbox is full.
        char topic[192];
        snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
                 gw_ha_retired_component(retired), node, gw_ha_retired_key(retired));
        if (esp_mqtt_client_enqueue(client, topic, "", 0, 1, 1, true) >= 0) ++retired;
      }
      // One discovery entity per tick bounds transient RAM/outbox use on this
      // no-PSRAM board. A full outbox retries this entity, preserving progress.
      if (cleared_state && retired == gw_ha_retired_count() && discovery < gw_ha_entity_count()) {
        char topic[192];
        snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
                 gw_ha_entity_component(discovery), node, gw_ha_entity_key(discovery));
        char *payload = gw_ha_discovery_json(discovery, node, state_topic, status_topic,
            esp_app_get_description()->version, serial, settings.period_s * 3 + 15);
        if (payload) {
          int result = esp_mqtt_client_enqueue(client, topic, payload, 0, 1, 1, true);
          cJSON_free(payload);
          if (result >= 0) ++discovery;
        }
      } else if (cleared_state && retired == gw_ha_retired_count() && !announced) {
        announced = esp_mqtt_client_enqueue(client, status_topic, "online", 0, 1, 1, true) >= 0;
        // Deliver initial values after all discovery subscriptions exist.
        if (announced) {
          next_state = next_heartbeat = diagnostics_retry = 0;
          diagnostics_sent = false;
        }
      }
      if (announced && now >= next_state) {
        char *payload = gw_ha_state_json();
        if (payload) {
          // Publish changed values, plus an unchanged heartbeat for expiry and
          // HA restarts. No advancing ages/attributes reach normal entities.
          if (!last_state || strcmp(last_state, payload) || now >= next_heartbeat) {
            // ESP-MQTT only queues QoS0 when store=true; retain stays false.
            if (esp_mqtt_client_enqueue(client, state_topic, payload, 0, 0, 0, true) >= 0) {
              cJSON_free(last_state); last_state = payload; payload = NULL;
              unsigned heartbeat_s = settings.period_s > GW_HA_HEARTBEAT_PERIOD_S
                  ? settings.period_s : GW_HA_HEARTBEAT_PERIOD_S;
              next_heartbeat = now + heartbeat_s * 1000;
            }
          }
          cJSON_free(payload);
        }
        next_state = now + settings.period_s * 1000;
      }
      if (announced && now >= diagnostics_retry &&
          gw_ha_diagnostics_due(now, diagnostics_at, diagnostics_sent)) {
        char *payload = gw_ha_diagnostics_json();
        bool queued = payload && esp_mqtt_client_enqueue(client, diagnostics_topic, payload, 0, 0, 0, true) >= 0;
        cJSON_free(payload);
        if (queued) { diagnostics_at = now; diagnostics_sent = true; }
        // Bound both active traffic and outbox/allocation retries to 1 Hz.
        diagnostics_retry = now + GW_HA_DIAGNOSTICS_ACTIVE_PERIOD_S * 1000;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/*****************************************************************************/

esp_err_t gw_ha_start(void)
{
  configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
  if (publisher_task) return ESP_ERR_INVALID_STATE;
  uint8_t mac[6];
  esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err != ESP_OK) return err;
  for (unsigned i = 0; i < sizeof(mac); ++i) snprintf(serial + 2 * i, 3, "%02x", mac[i]);
  snprintf(node, sizeof(node), "ecmpilot_gw_%s", serial);
  if (snprintf(state_topic, sizeof(state_topic), "%s/%s/state", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(state_topic) ||
      snprintf(command_topic, sizeof(command_topic), "%s/%s/command/request", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(command_topic) ||
      snprintf(response_topic, sizeof(response_topic), "%s/%s/command/response", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(response_topic) ||
      snprintf(command_info_topic, sizeof(command_info_topic), "%s/%s/command/info", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(command_info_topic) ||
      snprintf(diagnostics_topic, sizeof(diagnostics_topic), "%s/diagnostics", state_topic) >= sizeof(diagnostics_topic) ||
      snprintf(status_topic, sizeof(status_topic), "%s/%s/status", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(status_topic))
    return ESP_ERR_INVALID_SIZE;
  QueueHandle_t requests = xQueueCreate(2, sizeof(ha_request_t));
  QueueHandle_t replies = xQueueCreate(4, sizeof(ha_response_t));
  if (!requests || !replies) {
    if (requests) vQueueDelete(requests);
    if (replies) vQueueDelete(replies);
    return ESP_ERR_NO_MEM;
  }
  atomic_store(&request_queue, requests); atomic_store(&response_queue, replies);
  if (xTaskCreatePinnedToCore(publisher, "haPub", 6144, NULL, 1, &publisher_task, GW_NETWORK_CORE) != pdPASS)
    return ESP_ERR_NO_MEM;
  ESP_LOGI("ha", "HA telemetry ready: %u entities, node=%s", (unsigned)gw_ha_entity_count(), node);
  return ESP_OK;
}
