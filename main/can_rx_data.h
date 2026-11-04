// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool extended, rtr;
    uint8_t data[8];
} gw_can_frame_t;
typedef enum {
    GW_CAN_DISABLED, GW_CAN_INITIALIZING, GW_CAN_LISTENING,
    GW_CAN_STOPPED, GW_CAN_BUS_OFF, GW_CAN_RECOVERING, GW_CAN_FAULT,
} gw_can_state_t;
typedef struct {
    gw_can_state_t state;
    int32_t error;
    uint32_t missed, overrun, bus_errors, rx_errors, pending;
} gw_can_health_t;
// Called only by the CPU1 CAN process. No consumer callbacks or control logic.
void gw_can_rx_data_init(void);
void gw_can_rx_accept(const gw_can_frame_t *frame);
void gw_can_rx_publish_health(const gw_can_health_t *health);
#ifdef __cplusplus
}
#endif
