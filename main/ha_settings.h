// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t version, revision, enabled, port, period_s;
    char host[128], username[65], password[129];
} gw_ha_settings_t;
esp_err_t gw_ha_settings_init(void);
void gw_ha_settings_get(gw_ha_settings_t *out);
// Never returns the stored password. An omitted password preserves it;
// an explicit empty password clears it. Saves take effect without a reboot.
cJSON *gw_ha_settings_json(void);
esp_err_t gw_ha_settings_save(const cJSON *args, const char **reason);
#ifdef __cplusplus
}
#endif
