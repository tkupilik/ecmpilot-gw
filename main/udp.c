// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "process_data.h"
#include "udp_protocol.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static int udp_socket = -1;
static uint32_t received, sent, dropped;
static void count(size_t id, uint32_t *value)
{
    if (*value < INT32_MAX) ++*value;
    varstore_write(id, (int32_t)*value);
}
static void udp_task(void *unused)
{
    (void)unused;
    configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
    char input[GW_UDP_MAX_BYTES + 1], output[GW_UDP_MAX_BYTES + 1];
    uint64_t window = 0;
    unsigned requests = 0;
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int n = recvfrom(udp_socket, input, sizeof(input), 0, (struct sockaddr *)&peer, &peer_length);
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        gw_udp_protocol_tick(now);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                ESP_LOGW("udp", "Receive error %d", errno);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            continue;
        }
        count(GW_VAR_UDP_RX_COUNT, &received);
        if (now - window >= 1000) { window = now; requests = 0; }
        const uint32_t address = ntohl(peer.sin_addr.s_addr);
        if (++requests > 20 || n > GW_UDP_MAX_BYTES || !address || address >= 0xe0000000U) {
            count(GW_VAR_UDP_DROP_COUNT, &dropped);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t length = gw_udp_protocol_handle(input, (size_t)n, now, output);
        if (!length) { count(GW_VAR_UDP_DROP_COUNT, &dropped); continue; }
        int written = sendto(udp_socket, output, length, 0, (struct sockaddr *)&peer, peer_length);
        if (written == (int)length) count(GW_VAR_UDP_TX_COUNT, &sent);
        else count(GW_VAR_UDP_DROP_COUNT, &dropped);
    }
}
esp_err_t gw_udp_start(void)
{
    configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
    if (udp_socket >= 0) return ESP_ERR_INVALID_STATE;
    uint8_t mac[6], random[16];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    char identity[32], boot[33];
    snprintf(identity, sizeof(identity), "ecmpilot-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < sizeof(random); ++i) snprintf(boot + 2 * i, 3, "%02x", random[i]);
    gw_udp_protocol_init(identity, boot, esp_app_get_description()->version);
    varstore_write(GW_VAR_UDP_RX_COUNT, 0); varstore_write(GW_VAR_UDP_TX_COUNT, 0);
    varstore_write(GW_VAR_UDP_DROP_COUNT, 0);
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) return ESP_FAIL;
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 200000};
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(GW_UDP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(udp_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        bind(udp_socket, (struct sockaddr *)&address, sizeof(address))) {
        close(udp_socket); udp_socket = -1; return ESP_FAIL;
    }
    if (xTaskCreatePinnedToCore(udp_task, "udp", 8192, NULL, 4, NULL, GW_NETWORK_CORE) != pdPASS) {
        close(udp_socket); udp_socket = -1; return ESP_ERR_NO_MEM;
    }
    ESP_LOGI("udp", "%s listening on UDP %d, CPU%d; vehicle control unavailable; varstore test mode is opt-in",
             identity, GW_UDP_PORT, GW_NETWORK_CORE);
    return ESP_OK;
}
