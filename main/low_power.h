// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { GW_LOW_POWER_DISABLED, GW_LOW_POWER_WAIT_DATA, GW_LOW_POWER_COUNTING,
       GW_LOW_POWER_BUSY, GW_LOW_POWER_ENTERING, GW_LOW_POWER_RETRY,
       GW_LOW_POWER_UNAVAILABLE, GW_LOW_POWER_INVALID_CONFIG };
enum { GW_LOW_POWER_MAX_HOURS = 720 };
esp_err_t gw_low_power_init(void);
// Parallel PT in the CPU1 application process. No driver or network calls.
void gw_low_power_tick(void);
// Rechecked by the power owner during automatic sleep preparation.
bool gw_low_power_eligible(void);
// Recheck settings/work while RX is intentionally stopped; its cache is invalidated.
bool gw_low_power_permitted(void);
cJSON *gw_low_power_json(void);
esp_err_t gw_low_power_save(const cJSON *args, const char **reason);
#ifdef __cplusplus
}
#endif
