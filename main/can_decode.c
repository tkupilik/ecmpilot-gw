// SPDX-License-Identifier: BSD-2-Clause
#include "can_decode.h"
#include "can_info_decode.h"
#include "process_data.h"
void gw_can_hs1_decode_publish(const gw_can_frame_t *frame);

int gw_can_decode_publish(const gw_can_frame_t *frame)
{
    if (!frame || frame->extended || frame->rtr || frame->dlc > 8) return -1;
#if CONFIG_GW_CAN_INFO
    return gw_can_info_decode(frame->id, frame->data, frame->dlc, false, false);
#else
    unsigned expected;
    switch (frame->id) {
    case 0x5C3: case 0x3CE: case 0x412: case 0x5B2: case 0x552: expected = 8; break;
    case 0x56E: expected = 6; break;
    case 0x5A2: expected = 7; break;
    default: return 0;
    }
    if (frame->dlc != expected) return -1;
    gw_can_hs1_decode_publish(frame);
    return 1;
#endif
}
void gw_can_decode_reset(void)
{
    gw_can_info_reset();
    // Fields available only on the HSCAN1 profile also lose validity.
    varstore_invalidate(GW_VAR_VOLTAGE_MV);
    for (unsigned i = GW_VAR_CAN_HVAC_UPSTREAM_MC; i <= GW_VAR_CAN_ODOMETER_KM_CANDIDATE; ++i)
        varstore_invalidate(i);
}
