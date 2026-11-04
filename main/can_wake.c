// SPDX-License-Identifier: BSD-2-Clause
#include "can_wake.h"
#include "can_config.h"
#include "process_data.h"

void gw_can_wake_reset(void)
{
    varstore_write(GW_VAR_CAN_WAKE_TX_REQUEST_COUNT, 0);
    varstore_write(GW_VAR_CAN_VERSION_TX_REQUESTED, 0);
    varstore_write(GW_VAR_CAN_WAKE_TX_ID, CAN_WAKE_ID);
    varstore_write(GW_VAR_CAN_WAKE_TX_PAYLOAD_U16, 0x0080);
    varstore_write(GW_VAR_CAN_WAKE_TX_CONFIG_ERROR, ESP_OK);
    varstore_write(GW_VAR_CAN_WAKE_STATE, 0);
}
bool gw_can_wake_encode(uint32_t *identifier, uint16_t *payload)
{
    if (!GW_CAN_ENABLED || !GW_CAN_IS_INFO || CONFIG_GW_CAN_BITRATE != 125000 ||
        varstore_read(GW_VAR_CAN_REPLAY_ENABLED, 0, 0) || varstore_read(GW_VAR_TEST_ENABLED, 0, 0)) return false;
    int32_t id = varstore_read(GW_VAR_CAN_WAKE_TX_ID, 0, -1);
    int32_t value = varstore_read(GW_VAR_CAN_WAKE_TX_PAYLOAD_U16, 0, -1);
    if ((id != CAN_WAKE_ID && id != CAN_WAKE_ALT_ID) || value < 0 || value > 65535) return false;
    *identifier = (uint32_t)id; *payload = (uint16_t)value;
    return true;
}
