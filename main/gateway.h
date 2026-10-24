// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#define GW_NETWORK_CORE 0
#define GW_APP_CORE 1
#define GW_MAX_NETWORKS 8
#define GW_SSID_SIZE 33
#define GW_PASSWORD_SIZE 65

#if !CONFIG_IDF_TARGET_ESP32S3 || CONFIG_FREERTOS_UNICORE
#error "The gateway requires a dual-core ESP32-S3"
#endif
#if !CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0 || !CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0
#error "Pin Wi-Fi and TCP/IP to CPU0 in sdkconfig"
#endif
#if !CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1 || !CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU1
#error "Pin main and runtime timer tasks to CPU1 in sdkconfig"
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char ssid[GW_SSID_SIZE];
    char password[GW_PASSWORD_SIZE];
} gw_wifi_profile_t;

typedef struct {
    uint32_t version;
    uint32_t count;
    gw_wifi_profile_t networks[GW_MAX_NETWORKS];
} gw_settings_t;

typedef struct {
    bool connected;
    bool setup_ap;
    char ssid[GW_SSID_SIZE];
    char ip[16];
    char ap_ssid[GW_SSID_SIZE];
    int rssi;
    int last_disconnect_reason;
} gw_network_status_t;

esp_err_t gw_settings_init(void);
void gw_settings_get(gw_settings_t *settings);
esp_err_t gw_settings_save(const gw_settings_t *settings);
esp_err_t gw_network_start(void);
void gw_network_reconfigure(void);
void gw_network_status(gw_network_status_t *status);
esp_err_t gw_web_start(void);
esp_err_t gw_console_start(void);
void gw_restart_soon(void);
uint32_t gw_heartbeat_count(void);
#ifdef __cplusplus
}
#endif
