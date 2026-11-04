// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t gw_ha_start(void);
bool gw_ha_connected(void);
esp_err_t gw_ha_last_error(void);
#ifdef __cplusplus
}
#endif
