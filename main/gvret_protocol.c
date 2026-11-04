// SPDX-License-Identifier: BSD-2-Clause
// Independent implementation of the documented GVRET wire format. No upstream code copied.
#include "can_config.h"
#include "gvret_protocol.h"
#include <limits.h>
#include <string.h>

_Static_assert(sizeof(gw_gvret_frame_t) == 24, "Keep the diagnostic FIFO compact");

/*****************************************************************************/

static void Put32(uint8_t * p, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
}

/*****************************************************************************/

void gw_gvret_protocol_reset(gw_gvret_protocol_t * p)
{
  memset(p, 0, sizeof(*p));
}

/*****************************************************************************/

int gw_gvret_protocol_feed(gw_gvret_protocol_t * p, uint8_t byte, uint64_t now_us,
                             bool bus_running, uint8_t reply[GW_GVRET_REPLY_MAX])
{
  if (p->state && now_us - p->last_byte_us > 1000000U)
    return -1;
  p->last_byte_us = now_us;

  if (p->state == 2)
  {
    // SEND/ECHO header: ID[4], bus, DLC, followed by DLC payload bytes.
    if ((p->command == 0 || p->command == 11) && p->remaining == 1)
    {
      p->remaining = byte;
      p->state = byte ? 3 : 0;
      return 0;
    }
    if (!--p->remaining)
      p->state = 0;
    return 0;
  }
  if (p->state == 3)
  {
    if (!--p->remaining)
      p->state = 0;
    return 0;
  }
  if (!p->state)
  {
    if (byte == 0xE7)
      p->binary = true;
    else if (byte == 0xF1)
      p->state = 1;
    // Includes optional legacy trailing zero bytes and console noise.
    return 0;
  }

  p->state = 0;
  p->command = byte;
  memset(reply, 0, GW_GVRET_REPLY_MAX);
  reply[0] = 0xF1;
  reply[1] = byte;
  switch (byte)
  {
    case 1: // Same low32-bit boot-microsecond clock as received frames.
      Put32(reply + 2, (uint32_t)now_us);
      return 6;
    case 6: // Legacy reply always contains two slots; slot1 is absent/disabled.
      reply[2] = (bus_running ? 1 : 0) | (GW_CAN_IS_INFO ? 0 : 0x10);
      // Report actual controller mode; the TCP service still rejects all TX.
      Put32(reply + 3, GW_GVRET_BITRATE);
      return 12;
    case 7:
      reply[2] = 0;
      reply[3] = 1; // Adapter protocol build0x0100; no SD logging / single wire.
      return 8;
    case 9:
      reply[2] = 0xDE;
      reply[3] = 0xAD;
      return 4;
    case 12:
      reply[2] = 1;
      return 3;
    case 13: // Disabled SWCAN/LIN slots expected by the legacy client.
      return 17;
    case 0: case 11:
      p->remaining = 6;
      break;
    case 4: case 8: case 10:
      p->remaining = 1;
      break;
    case 5:
      p->remaining = 8;
      break;
    case 14:
      p->remaining = 12;
      break;
    default:
      return -1;
  }
  if (p->ignored_commands < INT32_MAX)
    ++p->ignored_commands;
  p->state = 2;
  return 0;
}

/*****************************************************************************/

size_t gw_gvret_encode(const gw_gvret_frame_t * frame, uint8_t out[GW_GVRET_FRAME_MAX])
{
  unsigned length = frame->tag & 15U;
  uint32_t id = frame->id & 0x7FFFFFFFU;
  if (length > 8 || id > ((frame->id & 0x80000000U) ? 0x1FFFFFFFU : 0x7FFU))
    return 0;
  out[0] = 0xF1;
  out[1] = 0;
  Put32(out + 2, (uint32_t)frame->timestamp_us);
  Put32(out + 6, frame->id);
  out[10] = length; // Bus0 in the high nibble.
  memcpy(out + 11, frame->data, length);
  out[11 + length] = 0; // Legacy GVRET trailer, not a TCP packet boundary.
  return 12 + length;
}
