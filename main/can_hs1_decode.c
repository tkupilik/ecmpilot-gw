// SPDX-License-Identifier: BSD-2-Clause
#include "can_decode.h"
#include "process_data.h"

// Factual wire layouts follow the pinned local HS1 DBC. Source provenance,
// confidence and Apache-2.0 definition notices: dbc/README.md. This independent
// decoder implementation is BSD-2-Clause; no upstream implementation is copied.

static void candidate(size_t id, unsigned raw, unsigned unavailable, int32_t value)
{
    if (raw == unavailable) varstore_invalidate(id);
    else varstore_write(id, value);
}

static void battery(const uint8_t *data)
{
    const unsigned voltage = ((unsigned)data[0] << 3) | (data[1] >> 5);
    const unsigned soc = ((unsigned)(data[2] & 3) << 8) | data[3];
    // Wake captures begin with a zero-filled payload. With no independently
    // verified availability bit, require plausible pack voltage (200..500 V).
    // This is a conservative startup gate, not a claim of an OEM validity rule.
    const bool available = voltage >= 800 && voltage <= 2000;
    if (available) varstore_write(GW_VAR_VOLTAGE_MV, (int32_t)voltage * 250);
    else varstore_invalidate(GW_VAR_VOLTAGE_MV);
    // 0% and 100% are legitimate with live pack voltage; 1023 is unavailable.
    if (available && soc <= 1000) varstore_write(GW_VAR_SOC_PCT_X10, (int32_t)soc);
    else varstore_invalidate(GW_VAR_SOC_PCT_X10);
}

static void hvac(const uint8_t *data)
{
    candidate(GW_VAR_CAN_HVAC_UPSTREAM_MC, data[1], 255, (int32_t)data[1] * 500 - 40000);
    candidate(GW_VAR_CAN_HVAC_HEATER_OUTLET_MC, data[2], 255, (int32_t)data[2] * 500 - 40000);
    candidate(GW_VAR_CAN_HVAC_HEATER_SETPOINT_MC, data[3], 255, (int32_t)data[3] * 500);
    const unsigned setpoint = data[4] >> 3;
    const unsigned probe = ((unsigned)(data[4] & 7) << 8) | data[5];
    const unsigned airflow = ((unsigned)data[6] << 1) | (data[7] >> 7);
    candidate(GW_VAR_CAN_HVAC_EVAPORATOR_SETPOINT_MC, setpoint, 31, (int32_t)setpoint * 500);
    candidate(GW_VAR_CAN_HVAC_EVAPORATOR_MC, probe, 2047, (int32_t)probe * 100 - 40000);
    candidate(GW_VAR_CAN_HVAC_AIRFLOW_GPS_X10, airflow, 511, (int32_t)airflow * 5);
}

void gw_can_hs1_decode_publish(const gw_can_frame_t *frame)
{
    if (!frame || frame->extended || frame->rtr) return;
    const uint8_t *data = frame->data;
    // Require the observed full DLC; truncated or colliding extended frames
    // neither publish nor refresh observations. No action/transmission here.
    switch (frame->id) {
    case 0x5C3:
        if (frame->dlc == 8) battery(data);
        break;
    case 0x3CE:
        if (frame->dlc == 8) hvac(data);
        break;
    case 0x56E:
        if (frame->dlc == 6) {
            const unsigned current = ((unsigned)(data[1] & 1) << 8) | data[2];
            candidate(GW_VAR_CAN_HVAC_COMPRESSOR_CURRENT_MA, current, 511, (int32_t)current * 100);
        }
        break;
    case 0x412:
        if (frame->dlc == 8) varstore_write(GW_VAR_CAN_HVAC_HEATING_REQUEST_RAW, (data[1] >> 3) & 1);
        break;
    case 0x5A2:
        if (frame->dlc == 7) {
            unsigned any = 0;
            for (unsigned i = 0; i < 7; ++i) any |= data[i];
            if (any) varstore_write(GW_VAR_CAN_HVAC_PRECONDITIONING_STATE_RAW, data[3] & 1);
            else varstore_invalidate(GW_VAR_CAN_HVAC_PRECONDITIONING_STATE_RAW);
        }
        break;
    case 0x5B2:
        if (frame->dlc == 8) {
            if (((data[6] >> 5) & 3) != 0) varstore_invalidate(GW_VAR_CAN_OUTSIDE_MC_CANDIDATE);
            else candidate(GW_VAR_CAN_OUTSIDE_MC_CANDIDATE, data[2], 255, (int32_t)data[2] * 500 - 40000);
        }
        break;
    case 0x552:
        if (frame->dlc == 8) {
            const unsigned km = ((unsigned)data[4] << 16) | ((unsigned)data[5] << 8) | data[6];
            candidate(GW_VAR_CAN_ODOMETER_KM_CANDIDATE, km, 0xFFFFFF, (int32_t)km);
        }
        break;
    default: break;
    }
}
