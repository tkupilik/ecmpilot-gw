// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gw_can_info_init(void);

// One owner: TaskCan. Only publishes observations; no UI/control callbacks.
void gw_can_info_reset(void);
// 1 = handled, 0 = unrelated, -1 = rejected format/DLC. Classic INFO CAN only.
int gw_can_info_decode(uint32_t id, const uint8_t * data, size_t length, bool extended, bool rtr);

#ifdef __cplusplus
}
#endif
