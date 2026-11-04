// SPDX-License-Identifier: BSD-2-Clause
#include "process_data.h"
#include "vehicle_data.h"
#include "can_wake.h"
#include "vehicle_control.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const int32_t offsets[] = {
  -7000, -6000, -5000, -4000, -3000, -2500, -2000, -1500, -1000, -500,
  0, 500, 1000, 1500, 2000, 2500, 3000, 4000, 5000, 6000, 7000
};

/*****************************************************************************/

static void update_setpoint(size_t output, size_t code_id)
{
  // Keep last known inputs and propagate their original age. Consumers decide
  // freshness; an idle bus must not turn cached numeric setpoints into nulls.
  varstore_snapshot_t reference = varstore_inspect(GW_VAR_BSI_CLIMATE_CENTER_SETPOINT_MC, 0);
  varstore_snapshot_t code = varstore_inspect(code_id, 0);
  if (reference.state == VARSTORE_INVALID || code.state == VARSTORE_INVALID ||
      reference.value < 18000 || reference.value > 25000 || code.value < 1 || code.value > 21)
  {
    varstore_invalidate(output);
    return;
  }
  uint32_t reference_age = reference.state == VARSTORE_FORCED ? 0 : reference.age_ms;
  uint32_t code_age = code.state == VARSTORE_FORCED ? 0 : code.age_ms;
  uint32_t age = reference_age > code_age ? reference_age : code_age;
  uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
  varstore_write_at(output, reference.value + offsets[code.value - 1], now >= age ? now - age : 0);
}

/*****************************************************************************/

#if CONFIG_GW_CAN_INFO
static void update_charge_state(void)
{
  // TaskCan produces only the source signal. The application owns normalization
  // for both UDP and HA. A repeated task tick must not make cached data younger.
  static const int32_t states[] = {0, 2, 4, 1, 3};
  varstore_snapshot_t hmi = varstore_inspect(GW_VAR_BSI_CHARGING_HMI_STATE, 0);
  if (hmi.state == VARSTORE_INVALID || hmi.value < 0 || hmi.value > 4)
  {
    varstore_invalidate(GW_VAR_CHARGE_STATE);
    varstore_invalidate(GW_VAR_CHARGE_CONNECTED);
    return;
  }
  uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
  uint32_t age = hmi.state == VARSTORE_FORCED ? 0 : hmi.age_ms;
  uint64_t time = now >= age ? now - age : 0;
  varstore_write_at(GW_VAR_CHARGE_STATE, states[hmi.value], time);
  // Derived HMI connection indication, not an independent cable sensor.
  varstore_write_at(GW_VAR_CHARGE_CONNECTED, hmi.value != 0, time);
}
#endif

/*****************************************************************************/

void gw_vehicle_init(void)
{
  gw_can_wake_reset();
  gw_vehicle_control_init();
}

void gw_vehicle_tick(void)
{
#if CONFIG_GW_CAN_INFO
  update_charge_state();
#endif
  update_setpoint(GW_VAR_CLIMATE_LEFT_SETPOINT_MC, GW_VAR_ICSV_CLIMATE_LEFT_TEMP_CODE);
  update_setpoint(GW_VAR_CLIMATE_RIGHT_SETPOINT_MC, GW_VAR_ICSV_CLIMATE_RIGHT_TEMP_CODE);
}
