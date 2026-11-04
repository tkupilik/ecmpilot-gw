// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "ha_bridge.h"

#include "ha_settings.h"
#include "ha_telemetry.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
bool gw_ha_connected(void) { return atomic_load(&connected); }
esp_err_t gw_ha_last_error(void) { return atomic_load(&last_error); }

/*****************************************************************************/

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
  (void)arg; (void)base; (void)data;
  if (event == MQTT_EVENT_CONNECTED) {
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
  }
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
  bool announced = false, cleared_state = false;
  uint64_t next_state = 0;
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
      }
      if (!cleared_state)
        cleared_state = esp_mqtt_client_enqueue(client, state_topic, "", 0, 1, 1, true) >= 0;
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
      }
      if (now >= next_state) {
        char *payload = gw_ha_state_json();
        if (payload) {
          // ESP-MQTT only queues QoS0 when store=true; retain stays false.
          esp_mqtt_client_enqueue(client, state_topic, payload, 0, 0, 0, true);
          cJSON_free(payload);
        }
        next_state = now + settings.period_s * 1000;
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
      snprintf(status_topic, sizeof(status_topic), "%s/%s/status", CONFIG_GW_HA_TOPIC_PREFIX, node) >= sizeof(status_topic))
    return ESP_ERR_INVALID_SIZE;
  if (xTaskCreatePinnedToCore(publisher, "haPub", 6144, NULL, 1, &publisher_task, GW_NETWORK_CORE) != pdPASS)
    return ESP_ERR_NO_MEM;
  ESP_LOGI("ha", "HA telemetry ready: %u entities, node=%s", (unsigned)gw_ha_entity_count(), node);
  return ESP_OK;
}
