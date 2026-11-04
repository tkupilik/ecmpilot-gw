// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include "power.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t gw_power_policy_init(void);
void gw_power_policy_tick(void);
esp_err_t gw_power_apply_profile(int minimum_mhz, int maximum_mhz, bool light_sleep);
#ifdef __cplusplus
}
#endif
