// SPDX-License-Identifier: BSD-2-Clause
#include "can_config.h"
#include "can_rx_data.h"
#include "can_decode.h"
#include "can_info_decode.h"
#include "can_replay.h"
#include "process_data.h"
#include <limits.h>

static uint32_t received, rejected, rtr_count, generation, heartbeat;
static uint32_t decoded, decode_rejected, replayed, replay_epoch;
static bool replay_enabled;
static uint32_t version_received;
static void increment(uint32_t *n) { if (*n < INT32_MAX) ++*n; }
static int32_t counter(uint32_t n) { return (int32_t)(n & INT32_MAX); }
void gw_can_rx_data_init(void)
{
    received = rejected = rtr_count = generation = heartbeat = 0;
    decoded = decode_rejected = replayed = replay_epoch = 0;
    replay_enabled = false;
    version_received = 0;
    varstore_write(GW_VAR_CAN_VERSION_RX_COUNT, 0);
    gw_can_info_init();
    gw_can_decode_reset();
    gw_can_replay_reset();
    varstore_write(GW_VAR_CAN_REPLAY_ENABLED, 0);
    varstore_write(GW_VAR_CAN_STATE, GW_CAN_INITIALIZING);
    varstore_write(GW_VAR_CAN_LAST_ERROR, 0);
    // Last-frame fields stay invalid until the first accepted data frame.
}
void gw_can_rx_accept(const gw_can_frame_t *frame)
{
    if (!frame || frame->dlc > 8 || frame->id > (frame->extended ? 0x1fffffffU : 0x7ffU)) {
        ++rejected;
        return;
    }
    ++received;
    if (frame->rtr) { ++rtr_count; return; }
    if (GW_CAN_IS_INFO && !frame->extended && frame->id == 0x5DF) {
        increment(&version_received);
        varstore_write(GW_VAR_CAN_VERSION_RX_COUNT, version_received);
    }
    // Odd/even publication generation for a coherent multi-signal raw dump.
    // This is a diagnostic last-frame sample, not an event queue for control.
    varstore_write(GW_VAR_CAN_RX_LAST_GENERATION, counter(++generation));
    varstore_write(GW_VAR_CAN_RX_LAST_ID, (int32_t)frame->id);
    varstore_write(GW_VAR_CAN_RX_LAST_DLC, frame->dlc);
    varstore_write(GW_VAR_CAN_RX_LAST_EXTENDED, frame->extended);
    for (unsigned i = 0; i < 8; ++i)
        varstore_write(GW_VAR_CAN_RX_LAST_DATA0 + i, i < frame->dlc ? frame->data[i] : 0);
    varstore_write(GW_VAR_CAN_RX_LAST_GENERATION, counter(++generation));
    if (GW_CAN_IS_INFO && varstore_read(GW_VAR_CAN_REPLAY_ENABLED, 0, 0) == 1) return;
    int result = gw_can_decode_publish(frame);
    if (result > 0) increment(&decoded);
    if (result < 0) increment(&decode_rejected);
}
void gw_can_rx_publish_health(const gw_can_health_t *health)
{
    varstore_write(GW_VAR_CAN_STATE, health->state);
    varstore_write(GW_VAR_CAN_LAST_ERROR, health->error);
    varstore_write(GW_VAR_CAN_RX_COUNT, counter(received));
    varstore_write(GW_VAR_CAN_RX_REJECTED_COUNT, counter(rejected));
    varstore_write(GW_VAR_CAN_RX_RTR_COUNT, counter(rtr_count));
    varstore_write(GW_VAR_CAN_RX_MISSED_COUNT, counter(health->missed));
    varstore_write(GW_VAR_CAN_RX_OVERRUN_COUNT, counter(health->overrun));
    varstore_write(GW_VAR_CAN_BUS_ERROR_COUNT, counter(health->bus_errors));
    varstore_write(GW_VAR_CAN_RX_ERROR_COUNTER, counter(health->rx_errors));
    varstore_write(GW_VAR_CAN_RX_PENDING, counter(health->pending));
    varstore_write(GW_VAR_CAN_HEARTBEAT, counter(++heartbeat));
    varstore_write(GW_VAR_CAN_DECODE_COUNT, decoded);
    varstore_write(GW_VAR_CAN_DECODE_REJECTED_COUNT, decode_rejected);
    varstore_write(GW_VAR_CAN_DEMO_FRAME_COUNT, replayed);
}

// Called by TaskCan on CPU1; replay never enters the physical capture stream.
void gw_can_rx_tick(uint32_t now_ms)
{
    bool enabled = GW_CAN_IS_INFO && varstore_read(GW_VAR_CAN_REPLAY_ENABLED, 0, 0) == 1;
    if (enabled != replay_enabled) {
        gw_can_decode_reset();
        gw_can_replay_reset();
        replay_epoch = now_ms;
        replay_enabled = enabled;
    }
    if (enabled) {
        unsigned count = gw_can_replay_step(now_ms - replay_epoch);
        replayed = count > INT32_MAX - replayed ? INT32_MAX : replayed + count;
    }
}
