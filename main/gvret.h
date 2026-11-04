// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize fixed DRAM storage before TaskCan starts; start the one socket task after Wi-Fi init.
esp_err_t gw_gvret_init(void);
esp_err_t gw_gvret_start(void);
// TaskCan's raw, physical RX tap. Never waits for a client or queue space.
void gw_gvret_capture(uint32_t id, const uint8_t * data, size_t length,
                       bool extended, bool rtr, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
