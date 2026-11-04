// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { GW_AUTO_DISABLED, GW_AUTO_COUNTING, GW_AUTO_LOW_BATTERY,
       GW_AUTO_UNAVAILABLE, GW_AUTO_BUSY, GW_AUTO_INVALID_CONFIG, GW_AUTO_EXECUTING };
enum { GW_AUTO_MAX_PERIOD_MIN = 1440 };
enum { GW_LIMIT_DISABLED, GW_LIMIT_MONITORING, GW_LIMIT_WAIT_DATA, GW_LIMIT_BUSY,
       GW_LIMIT_REQUESTING, GW_LIMIT_REACHED, GW_LIMIT_RETRY, GW_LIMIT_UNAVAILABLE,
       GW_LIMIT_LOW_BATTERY };
typedef struct {
    uint32_t version, revision, enabled, standby_min, charging_min, min_aux_soc_pct_x10;
    uint32_t charge_limit_enabled, charge_limit_soc_pct_x10;
} gw_auto_refresh_config_t;
// Called after varstore initialization; configuration uses its own NVS key.
esp_err_t gw_auto_refresh_init(void);
// Lock-free task-side read of the committed varstore configuration, including Force.
bool gw_auto_refresh_config(gw_auto_refresh_config_t *out);
cJSON *gw_auto_refresh_json(void);
// Full compare-and-set. Repeating the same desired settings is idempotent.
esp_err_t gw_auto_refresh_save(const cJSON *args, const char **reason);
#ifdef __cplusplus
}
#endif
