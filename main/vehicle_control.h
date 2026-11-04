// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Commands are committed by UDP, consumed by the vehicle process. CAN RX only
// publishes observations. No socket or TWAI dependency in this policy module.
enum { GW_CONTROL_REFRESH, GW_CONTROL_IMMEDIATE, GW_CONTROL_DEFERRED,
       GW_CONTROL_PRECOND_START, GW_CONTROL_PRECOND_STOP, GW_CONTROL_PRECOND_PROGRAMS };
enum { GW_CONTROL_IDLE, GW_CONTROL_WAIT_BSI, GW_CONTROL_REFRESH_TX,
       GW_CONTROL_WAIT_DATA, GW_CONTROL_MODE_TX, GW_CONTROL_WAIT_MODE,
       GW_CONTROL_SUCCEEDED, GW_CONTROL_FAILED, GW_CONTROL_UNCONFIRMED,
       GW_CONTROL_WAIT_PROGRAMS, GW_CONTROL_PRECOND_TX,
       GW_CONTROL_PRECOND_AUX_TX, GW_CONTROL_WAIT_PRECOND };
enum { GW_CONTROL_RESULT_NONE, GW_CONTROL_RESULT_OK, GW_CONTROL_RESULT_WAKE_TIMEOUT,
       GW_CONTROL_RESULT_TX_TIMEOUT, GW_CONTROL_RESULT_REPLY_TIMEOUT,
       GW_CONTROL_RESULT_CONFIG, GW_CONTROL_RESULT_NOT_READY,
       GW_CONTROL_RESULT_OPERATION_TIMEOUT, GW_CONTROL_RESULT_UNAVAILABLE,
       GW_CONTROL_RESULT_MOVING, GW_CONTROL_RESULT_TX_FAILED,
       GW_CONTROL_RESULT_PRECOND_UNAVAILABLE, GW_CONTROL_RESULT_PRECOND_REJECTED,
       GW_CONTROL_RESULT_PROGRAMS_TIMEOUT, GW_CONTROL_RESULT_PROGRAMS_CONFLICT };
enum { GW_CONTROL_TICK_MS = 20, GW_CONTROL_WAKE_INTERVAL_MS = 100,
       GW_CONTROL_WAKE_MAX = 10, GW_CONTROL_SETTLE_MS = 200,
       GW_CONTROL_BSI_MAX_AGE_MS = 300, GW_CONTROL_TX_TIMEOUT_MS = 200,
       GW_CONTROL_REFRESH_TIMEOUT_MS = 3000, GW_CONTROL_REFRESH_MAX = 3,
       GW_CONTROL_MODE_TIMEOUT_MS = 1000, GW_CONTROL_MODE_MAX = 3,
       GW_CONTROL_PREAMBLE_MS = 460, GW_CONTROL_OPERATION_TIMEOUT_MS = 25000,
       GW_CONTROL_PROGRAMS_TIMEOUT_MS = 1500, GW_CONTROL_PRECOND_ACK_MS = 1500,
       GW_CONTROL_PRECOND_MAX = 3, GW_CONTROL_PRECOND_TIMEOUT_MS = 30000 };
void gw_vehicle_control_init(void);
void gw_vehicle_control_tick(void);
bool gw_vehicle_control_available(void);
// Mailbox consumed only by TaskCan; result is published after driver submission.
bool gw_vehicle_control_prepare(uint32_t *consumed, uint32_t *id, uint8_t *length, uint8_t data[8]);
void gw_vehicle_control_submitted(uint32_t generation, esp_err_t error);
#ifdef __cplusplus
}
#endif
