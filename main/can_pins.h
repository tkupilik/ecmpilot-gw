// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "driver/gpio.h"

// Shared by the TWAI owner and the CAN-only deep-sleep wake path.
#define GW_CAN_TX_GPIO GPIO_NUM_15
#define GW_CAN_RX_GPIO GPIO_NUM_16
