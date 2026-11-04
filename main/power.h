// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { GW_POWER_NORMAL = 0, GW_POWER_DFS = 1, GW_POWER_LIGHT_SLEEP = 2, GW_POWER_CAN_DEEP_SLEEP = 3 };
enum { GW_DEEP_IDLE = 0, GW_DEEP_STOP_CAN = 1, GW_DEEP_STOP_NETWORK = 2,
       GW_DEEP_ENTER = 3, GW_DEEP_RESTORE = 4 };
esp_err_t gw_power_start(void);
// Called by the existing CPU1 application process, never by CAN RX.
void gw_power_tick(void);
// Single producer: HTTP task. Requests and results cross the raw varstore.
esp_err_t gw_power_request(int mode);

// CPU1 hardware boundary. CAN/network owners must acknowledge suspension first.
// Returns only on preparation failure; a successful wake causes a fresh boot.
esp_err_t gw_power_enter_can_deep_sleep(void);

#ifdef __cplusplus
}
#endif
