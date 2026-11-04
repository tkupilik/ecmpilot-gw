// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"

#define GW_GVRET_PORT 23
#define GW_GVRET_BITRATE CONFIG_GW_CAN_BITRATE
#define GW_GVRET_QUEUE_LENGTH 64
#define GW_GVRET_REPLY_MAX 20
#define GW_GVRET_FRAME_MAX 20

// Fixed-size FIFO item. ID bit31 is the GVRET extended flag; tag is session<<4|DLC.
typedef struct
{
  uint64_t timestamp_us;
  uint32_t id;
  uint32_t tag;
  uint8_t data[8];
} gw_gvret_frame_t;

typedef struct
{
  uint64_t last_byte_us;
  uint32_t ignored_commands;
  uint16_t remaining;
  uint8_t command;
  uint8_t state;
  bool binary;
} gw_gvret_protocol_t;

void gw_gvret_protocol_reset(gw_gvret_protocol_t * p);
// >=0 = reply length; -1 = unknown command/partial-command timeout (close session).
int gw_gvret_protocol_feed(gw_gvret_protocol_t * p, uint8_t byte, uint64_t now_us,
                             bool bus_running, uint8_t reply[GW_GVRET_REPLY_MAX]);
size_t gw_gvret_encode(const gw_gvret_frame_t * frame, uint8_t out[GW_GVRET_FRAME_MAX]);
