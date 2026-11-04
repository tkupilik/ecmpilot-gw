// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { CAN_WAKE_ID = 0x43C, CAN_WAKE_ALT_ID = 0x401 };
void gw_can_wake_reset(void);
// Config/encoding only. Scheduling and retries belong to the vehicle PT.
bool gw_can_wake_encode(uint32_t *identifier, uint16_t *payload);
#ifdef __cplusplus
}
#endif
