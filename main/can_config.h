// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "sdkconfig.h"

// Kconfig omits disabled booleans. Use explicit0/1 constants in C expressions.
#if CONFIG_GW_CAN_INFO
#define GW_CAN_IS_INFO 1
#else
#define GW_CAN_IS_INFO 0
#endif
#if CONFIG_GW_CAN_RX_ENABLED
#define GW_CAN_ENABLED 1
#else
#define GW_CAN_ENABLED 0
#endif
