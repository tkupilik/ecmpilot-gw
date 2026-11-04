// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void gw_can_replay_reset(void);
// Called by TaskCan only. Returns the number of internally decoded frames.
unsigned gw_can_replay_step(uint32_t elapsed_ms);
unsigned gw_can_replay_count(void);
uint32_t gw_can_replay_duration(void);
#ifdef __cplusplus
}
#endif
