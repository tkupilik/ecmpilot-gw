// SPDX-License-Identifier: BSD-2-Clause
#include "can_replay.h"
#include "can_info_decode.h"
#include <stddef.h>

typedef struct
{
  uint32_t ms;
  uint16_t id;
  uint8_t length;
  uint8_t data[8];
} ReplayFrame;

#include "can_replay_data.inc"

static size_t cursor;
static uint32_t previous;
static uint32_t cycle;

/*****************************************************************************/

void gw_can_replay_reset(void)
{
  cursor = 0;
  previous = 0;
  cycle = 0;
}

/*****************************************************************************/

unsigned gw_can_replay_step(uint32_t elapsed_ms)
{
  uint32_t position = elapsed_ms % CAN_REPLAY_DURATION_MS;
  if (elapsed_ms / CAN_REPLAY_DURATION_MS != cycle || position < previous)
  {
    gw_can_replay_reset();
    gw_can_info_reset();
  }
  cycle = elapsed_ms / CAN_REPLAY_DURATION_MS;
  previous = position;
  unsigned count = 0;
  // Bounded catch-up work keeps other tasks schedulable after delayed ticks.
  while (cursor < sizeof(frames) / sizeof(frames[0]) && frames[cursor].ms <= position && count < 32)
  {
    const ReplayFrame * frame = &frames[cursor++];
    gw_can_info_decode(frame->id, frame->data, frame->length, false, false);
    ++count;
  }
  return count;
}

/*****************************************************************************/

unsigned gw_can_replay_count(void) { return sizeof(frames) / sizeof(frames[0]); }
uint32_t gw_can_replay_duration(void) { return CAN_REPLAY_DURATION_MS; }
