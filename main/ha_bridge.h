// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t gw_ha_start(void);
bool gw_ha_connected(void);
esp_err_t gw_ha_last_error(void);
// Called only by the UDP task, the shared explicit-command owner. MQTT event
// callbacks only copy bounded requests into its queue; they never drive CAN.
void gw_ha_process_commands(uint64_t now_ms);
#ifdef __cplusplus
}
#endif
