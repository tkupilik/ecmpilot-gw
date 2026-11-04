// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "can_rx_data.h"
#include "sdkconfig.h"
#ifdef __cplusplus
extern "C" {
#endif
// Single TaskCan producer. Returns 1 handled, 0 unrelated, -1 invalid DLC/format.
int gw_can_decode_publish(const gw_can_frame_t *frame);
void gw_can_decode_reset(void);
#ifdef __cplusplus
}
#endif
