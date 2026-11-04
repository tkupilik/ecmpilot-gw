// SPDX-License-Identifier: Apache-2.0
// Signal definitions adapted from prototux/PSA-RE, revision 67f7581e7622410efdbb29c7eda3b86982b9b450.
// See docs/can-rx.md and licenses/NOTICE-CAN.txt. Field-level owner validation is recorded in docs/lcd-backport.md.
#include "process_data.h"
#include "esp_timer.h"
#include "can_info_decode.h"
#define NBI(a) (sizeof(a)/sizeof((a)[0]))
#include <limits.h>

/*****************************************************************************/

static uint32_t bsi_generation, soc_generation, schedule_generation;
static uint32_t precond_generation, programs34_generation;
static void Received(size_t id, uint32_t *generation)
{
  *generation = *generation == INT32_MAX ? 1 : *generation + 1;
  varstore_write(id, (int32_t)*generation);
}

static uint32_t buttons_previous;
static uint32_t buttons_time;
static bool buttons_seen;
static int32_t button_presses[9];

static const uint32_t outputs[] = {
  GW_VAR_CLIMATE_STATE, GW_VAR_BSI_PRECOND_STATUS_RAW, GW_VAR_BSI_PRECOND_AVAILABLE,
  GW_VAR_BSI_PRECOND_ASAP, GW_VAR_BSI_PRECOND_FEEDBACK,
  GW_VAR_BSI_PRECOND_PROGRAMS12_HI24, GW_VAR_BSI_PRECOND_PROGRAMS12_LO24,
  GW_VAR_BSI_PRECOND_RX_GENERATION, GW_VAR_BSI_PRECOND_PROGRAMS34_HI24,
  GW_VAR_BSI_PRECOND_PROGRAMS34_LO24, GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION,
  GW_VAR_SPEED_KMH, GW_VAR_ODOMETER_KM, GW_VAR_SOC_PCT_X10, GW_VAR_RANGE_KM_X10, GW_VAR_OUTSIDE_MC,
  GW_VAR_GEAR, GW_VAR_DRIVE_READY, GW_VAR_LIGHT_LOW, GW_VAR_SEATBELT_WARNING,
  GW_VAR_PARKING_BRAKE, GW_VAR_SPEED_LIMIT_KMH, GW_VAR_TRACTION_POWER_W,
  GW_VAR_VEHICLE_AWAKE, GW_VAR_VEHICLE_PARKED, GW_VAR_BATTERY_POWER_W,
  GW_VAR_DRIVE_POWER_PCT_X10, GW_VAR_CAN_GEAR_CODE, GW_VAR_CAN_POWER_RAW,
  GW_VAR_CAN_NETWORK_PHASE, GW_VAR_SW_BUTTONS, GW_VAR_SW_NAV_COUNTER,
  GW_VAR_SW_VOLUME_COUNTER, GW_VAR_CMB_NAV_COUNTER, GW_VAR_SW_OVERFLOW_FLAGS,
  GW_VAR_BSI_DAYNIGHT_STATUS, GW_VAR_BSI_BRIGHTNESS,
  GW_VAR_ICSV_CLIMATE_LEFT_TEMP_CODE, GW_VAR_ICSV_CLIMATE_RIGHT_TEMP_CODE,
  GW_VAR_ICSV_EVAPORATOR_MC, GW_VAR_CHARGE_MODE, GW_VAR_CHARGE_START_MINUTE,
  GW_VAR_BSI_CHARGE_SCHEDULE_RAW, GW_VAR_BSI_CHARGE_SCHEDULE_AVAILABLE,
  GW_VAR_BSI_TRIP_CHARGE_MODE, GW_VAR_BSI_TRIP_CHARGE_MINUTE,
  GW_VAR_BSI_STATUS_RX_GENERATION, GW_VAR_SOC_RX_GENERATION, GW_VAR_BSI_SCHEDULE_RX_GENERATION,
};

/*****************************************************************************/
// Motorola DBC start bit, MSB-first. Callers validate complete DLC first.

static unsigned Bits(const uint8_t * data, unsigned start, unsigned length)
{
  unsigned value = 0;
  while (length--)
  {
    value = (value << 1) | ((data[start / 8] >> (start % 8)) & 1U);
    start = start % 8 ? start - 1 : start + 15;
  }
  return value;
}

/*****************************************************************************/

static void Reading(uint32_t var, int32_t value, bool valid)
{
  if (valid)
    varstore_write(var, value);
  else
    varstore_invalidate(var);
}

/*****************************************************************************/

void gw_can_info_reset(void)
{
  for (size_t i = 0; i < NBI(outputs); ++i)
    varstore_invalidate(outputs[i]);
  for (unsigned id = GW_VAR_BSI_POWER_MANAGEMENT; id <= GW_VAR_BSI_CLIMATE_CABIN_AIR_QUALITY_LEVEL; ++id)
    varstore_invalidate(id);
  buttons_seen = false;
  // Event totals survive demo/live transitions; state baselines do not.
  for (size_t i = 0; i < NBI(button_presses); ++i)
    varstore_write(GW_VAR_SW_PRESS_COUNT + i, button_presses[i]);
}

/*****************************************************************************/

static void On036(const uint8_t * data)
{
  unsigned phase = data[4] & 7U;
  varstore_write(GW_VAR_CAN_NETWORK_PHASE, phase);
  Reading(GW_VAR_BSI_POWER_MANAGEMENT, phase, phase <= 4);
  varstore_write(GW_VAR_BSI_DAYNIGHT_STATUS, (data[3] >> 5) & 1U);
  varstore_write(GW_VAR_BSI_BRIGHTNESS, data[3] & 15U);
  // Do not infer sleep from missing traffic. Only explicit network phases.
  Reading(GW_VAR_VEHICLE_AWAKE, phase == 1 || phase == 3, phase <= 4);
  Received(GW_VAR_BSI_STATUS_RX_GENERATION, &bsi_generation);
}

/*****************************************************************************/

static void On0A2(const uint8_t * data)
{
  // Bits 0..5 = OK/back/menu/mode/left/voice, 6 = phone, 7..8 = CMB OK/back.
  uint32_t mask = ((data[1] >> 5) & 1U) | (((data[1] >> 4) & 1U) << 1) |
    (((data[1] >> 3) & 1U) << 2) | (((data[1] >> 2) & 1U) << 3) |
    (((data[1] >> 1) & 1U) << 4) | ((data[1] & 1U) << 5) |
    (((data[2] >> 7) & 1U) << 6) | (((data[4] >> 5) & 1U) << 7) |
    (((data[4] >> 4) & 1U) << 8);
  uint32_t now = (uint32_t)esp_timer_get_time();
  // First frame and a gap >1 second establish a baseline, not synthetic presses.
  uint32_t rising = buttons_seen && (uint32_t)(now - buttons_time) <= 1000000U
    ? mask & ~buttons_previous : 0;
  for (unsigned i = 0; i < NBI(button_presses); ++i)
  {
    if ((rising & (1U << i)) && button_presses[i] < INT32_MAX)
      ++button_presses[i];
    varstore_write(GW_VAR_SW_PRESS_COUNT + i, button_presses[i]);
  }
  buttons_previous = mask;
  buttons_time = now;
  buttons_seen = true;
  varstore_write(GW_VAR_SW_BUTTONS, mask);
  varstore_write(GW_VAR_SW_NAV_COUNTER, data[0] < 128 ? data[0] : (int)data[0] - 256);
  varstore_write(GW_VAR_CMB_NAV_COUNTER, data[3] < 128 ? data[3] : (int)data[3] - 256);
  varstore_write(GW_VAR_SW_VOLUME_COUNTER, data[5] < 128 ? data[5] : (int)data[5] - 256);
  varstore_write(GW_VAR_SW_OVERFLOW_FLAGS, ((data[1] >> 6) & 3U) | ((data[4] & 0xC3U) << 2));
}

/*****************************************************************************/

static void On0B6(const uint8_t * data)
{
  unsigned speed = Bits(data, 23, 16);
  // Keep the existing GUI input in whole km/h, nearest km/h.
  Reading(GW_VAR_SPEED_KMH, (speed + 50) / 100, speed != 65535);
}

/*****************************************************************************/

static void On0F6(const uint8_t * data)
{
  unsigned odo = Bits(data, 23, 24);
  Reading(GW_VAR_ODOMETER_KM, odo / 10, odo != 0xFFFFFF);
  // Use the filtered temperature; never interpret sensor/default codes as heat.
  Reading(GW_VAR_OUTSIDE_MC, (int32_t)data[6] * 500 - 40000, data[6] <= 250);
}

/*****************************************************************************/

static void On11C(const uint8_t * data)
{
  // Source-defined sign value/type; no sample of this ID in the current capture.
  Reading(GW_VAR_SPEED_LIMIT_KMH, data[2], data[2] >= 1 && data[2] <= 250 && (data[3] >> 6) == 0);
}

/*****************************************************************************/

static void On128(const uint8_t * data)
{
  unsigned raw = data[1] >> 4;
  int gear = raw <= 3 ? (int)raw : raw == 15 ? 4 : -1;
  varstore_write(GW_VAR_CAN_GEAR_CODE, raw);
  Reading(GW_VAR_GEAR, gear, gear >= 0);
  Reading(GW_VAR_VEHICLE_PARKED, raw == 0, gear >= 0);
  varstore_write(GW_VAR_DRIVE_READY, (data[7] >> 2) & 1U);
  varstore_write(GW_VAR_LIGHT_LOW, (data[0] >> 6) & 1U);
  varstore_write(GW_VAR_PARKING_BRAKE, (data[3] >> 1) & 1U);
  varstore_write(GW_VAR_SEATBELT_WARNING, (data[5] >> 6) & 1U);
}

/*****************************************************************************/

static void On1E7(const uint8_t * data)
{
  unsigned battery = data[3] >> 1;
  unsigned drive = Bits(data, 24, 9);
  varstore_write(GW_VAR_CAN_POWER_RAW, drive);
  Reading(GW_VAR_BATTERY_POWER_W, ((int32_t)battery - 50) * 1000, battery <= 100);
  // This is a percentage, NOT watts. Never invent motor power from it.
  Reading(GW_VAR_DRIVE_POWER_PCT_X10, (int32_t)drive * 5 - 500, drive <= 500);
}

/*****************************************************************************/

static void On31A(const uint8_t * data)
{
  unsigned average1 = Bits(data, 23, 9), average2 = Bits(data, 30, 9);
  unsigned instant = Bits(data, 37, 9);
  Reading(GW_VAR_BSI_TRIP1_AVERAGE_X10, average1, average1 <= 500);
  Reading(GW_VAR_BSI_TRIP2_AVERAGE_X10, average2, average2 <= 500);
  Reading(GW_VAR_BSI_TRIP_INSTANT_X10, instant, instant <= 500);
  unsigned range = Bits(data, 7, 9);
  // This Corsa-e uses 2 km/count: raw60 in info_soc57_refresh.csv matches
  // MyOpel 120 km at 57% SOC. Store tenths of km; upstream factor1 is wrong here.
  Reading(GW_VAR_RANGE_KM_X10, range * 20, range <= 500);
  varstore_write(GW_VAR_BSI_TRIP_CHARGE_MODE, (data[7] >> 2) & 1U);
  unsigned minute = ((data[6] & 0x3FU) << 5) | (data[7] >> 3);
  Reading(GW_VAR_BSI_TRIP_CHARGE_MINUTE, minute, minute < 1440);
}

/*****************************************************************************/

static void On236(const uint8_t * data)
{
  varstore_write(GW_VAR_BSI_DOOR_DRIVER_OPEN, (data[5] >> 6) & 1U);
  varstore_write(GW_VAR_BSI_DOOR_PASSENGER_OPEN, (data[7] >> 1) & 1U);
  varstore_write(GW_VAR_BSI_DOOR_REAR_LEFT_OPEN, (data[7] >> 4) & 1U);
  varstore_write(GW_VAR_BSI_DOOR_REAR_RIGHT_OPEN, (data[7] >> 5) & 1U);
  varstore_write(GW_VAR_BSI_TRUNK_OPEN, (data[5] >> 7) & 1U);
}

/*****************************************************************************/

static void On276(const uint8_t * data)
{
  unsigned state = data[1] >> 5;
  Reading(GW_VAR_BSI_LOCKING_STATE, state, state >= 1 && state <= 6);
}

/*****************************************************************************/

static void OnTrip(const uint8_t * data, bool second)
{
  unsigned distance = ((unsigned)data[1] << 8) | data[2];
  Reading(second ? GW_VAR_BSI_TRIP2_DISTANCE_KM : GW_VAR_BSI_TRIP1_DISTANCE_KM,
          distance, distance != 65535);
  Reading(second ? GW_VAR_BSI_TRIP2_SPEED_KMH : GW_VAR_BSI_TRIP1_SPEED_KMH,
          data[0], data[0] != 255);
  // The fuel-consumption words in these legacy layouts are not EV kWh.
}

/*****************************************************************************/

static void On39A(const uint8_t * data)
{
  for (unsigned i = 0; i < 6; ++i)
  {
    unsigned value = Bits(data, (i * 6 / 8) * 8 + 7 - i * 6 % 8, 6);
    Reading(GW_VAR_BSI_CONSUMPTION_HISTORY1_X10 + i, value * 10, value <= 50);
  }
}

/*****************************************************************************/


/*****************************************************************************/
// BSI 12V battery observations. Keep separate from traction-battery telemetry.

static void On160(const uint8_t * data)
{
  unsigned value = ((unsigned)data[0] << 8) | data[1];
  unsigned minute = value & 0x7FF;
  varstore_write(GW_VAR_BSI_CHARGE_SCHEDULE_RAW, value);
  varstore_write(GW_VAR_BSI_CHARGE_SCHEDULE_AVAILABLE, (value >> 15) & 1U);
  Reading(GW_VAR_CHARGE_MODE, (value >> 14) & 1U, (value & 0x8000) && minute < 1440);
  Reading(GW_VAR_CHARGE_START_MINUTE, minute, (value & 0x8000) && minute < 1440);
  Received(GW_VAR_BSI_SCHEDULE_RX_GENERATION, &schedule_generation);
}

static void On396(const uint8_t * data)
{
  unsigned voltage = data[1] >> 1;
  Reading(GW_VAR_BSI_AUX_BATTERY_VOLTAGE_MV, voltage * 100 + 5500, voltage <= 126);
  Reading(GW_VAR_BSI_AUX_BATTERY_SOC_PCT_X10, data[2] * 5, data[2] <= 200);
  unsigned auth = data[0] >> 6;
  Reading(GW_VAR_BSI_REMOTE_DIAG_AUTHORIZATION, auth, auth != 2);
  // Vehicle-specific INFO overlay of the source-reserved span, corroborated by
  // charging/delayed/driving captures and HS3/396 CHARGING_HMI_STATE. Do not
  // import the other HS3 fields by matching ID; raw5..7 remain visible unknowns.
  varstore_write(GW_VAR_BSI_CHARGING_HMI_STATE, (data[0] >> 2) & 7U);
}

/*****************************************************************************/
// Front climate status. Raw setpoint codes are relative to the 2AD reference;
// their combination belongs to the independent vehicle-data process.

static void On350(const uint8_t * data)
{
  varstore_write(GW_VAR_ICSV_CLIMATE_LEFT_TEMP_CODE, data[3] & 31U);
  varstore_write(GW_VAR_ICSV_CLIMATE_RIGHT_TEMP_CODE, data[4] & 31U);
  unsigned evaporator = ((unsigned)data[1] << 8) | data[2];
  Reading(GW_VAR_ICSV_EVAPORATOR_MC, (int32_t)evaporator * 100 - 40000, evaporator <= 1250);
}

/*****************************************************************************/
// BSI HMI requests/configuration. These are not measured HVAC operating state.

static void On2AD(const uint8_t * data)
{
  unsigned raw;
  raw = Bits(data, 7, 1);
  Reading(GW_VAR_BSI_CLIMATE_AC_LCD_DISPLAY_OFF_REQUEST, (int32_t)raw, raw <= 1);
  raw = Bits(data, 6, 1);
  Reading(GW_VAR_BSI_CLIMATE_RESTORE_MODE_ENABLED, (int32_t)raw, raw <= 1);
  raw = Bits(data, 5, 1);
  Reading(GW_VAR_BSI_CLIMATE_LUCH_WARNING_LAMP, (int32_t)raw, raw <= 1);
  raw = Bits(data, 4, 3);
  Reading(GW_VAR_BSI_CLIMATE_CENTER_SETPOINT_MC, (int32_t)raw * 1000 + 18000, raw <= 7);
  raw = Bits(data, 1, 2);
  Reading(GW_VAR_BSI_CLIMATE_AC_SYSTEM_TYPE, (int32_t)raw, raw <= 3);
  raw = Bits(data, 15, 1);
  Reading(GW_VAR_BSI_CLIMATE_HEATED_WINDSHIELD_INDICATOR_REQUEST, (int32_t)raw, raw <= 1);
  raw = Bits(data, 14, 2);
  Reading(GW_VAR_BSI_CLIMATE_AIR_INLET_SETPOINT, (int32_t)raw, raw <= 2);
  raw = Bits(data, 12, 2);
  Reading(GW_VAR_BSI_CLIMATE_HEATED_VENTILATED_SEATS_CONFIG, (int32_t)raw, raw <= 2);
  raw = Bits(data, 10, 1);
  Reading(GW_VAR_BSI_CLIMATE_HEATED_WINDSHIELD_PRESENCE, (int32_t)raw, raw <= 1);
  raw = Bits(data, 9, 1);
  Reading(GW_VAR_BSI_CLIMATE_REAR_CENTRAL_DISPLAY_PRESENCE, (int32_t)raw, raw <= 1);
  raw = Bits(data, 23, 8);
  Reading(GW_VAR_BSI_CLIMATE_FRONT_BLOWER_MOTOR_SETPOINT_PCT_X10, (int32_t)raw * 10 + 0, raw <= 100);
  raw = Bits(data, 31, 4);
  Reading(GW_VAR_BSI_CLIMATE_FRONT_BLOWER_PLUS_SWITCH_STATE, (int32_t)raw, raw >= 2 && raw <= 9);
  raw = Bits(data, 27, 4);
  Reading(GW_VAR_BSI_CLIMATE_FRONT_BLOWER_MINUS_STATE, (int32_t)raw, raw >= 2 && raw <= 9);
  raw = Bits(data, 39, 4);
  Reading(GW_VAR_BSI_CLIMATE_REAR_BLOWER_PLUS_SWITCH_STATE, (int32_t)raw, raw >= 2 && raw <= 9);
  raw = Bits(data, 35, 4);
  Reading(GW_VAR_BSI_CLIMATE_REAR_LEFT_BLOWER_MINUS_STATE, (int32_t)raw, raw >= 2 && raw <= 9);
  raw = Bits(data, 47, 2);
  Reading(GW_VAR_BSI_CLIMATE_IONIZER_STATE, (int32_t)raw, raw <= 2);
  raw = Bits(data, 45, 2);
  Reading(GW_VAR_BSI_CLIMATE_IONIZER_COMMAND, (int32_t)raw, raw <= 2);
  raw = Bits(data, 43, 2);
  Reading(GW_VAR_BSI_CLIMATE_CABIN_AIR_FILTER_STATE, (int32_t)raw, raw <= 2);
  raw = Bits(data, 41, 2);
  Reading(GW_VAR_BSI_CLIMATE_REAR_HEATED_VENTILATED_SEATS_CONFIG, (int32_t)raw, raw <= 2);
  raw = Bits(data, 55, 8);
  Reading(GW_VAR_BSI_CLIMATE_REAR_LEFT_BLOWER_SETPOINT_PCT_X10, (int32_t)raw * 10 + 0, raw <= 100);
  raw = Bits(data, 63, 2);
  Reading(GW_VAR_BSI_CLIMATE_CABIN_AIR_PURIFICATION_STATE, (int32_t)raw, raw <= 2);
  raw = Bits(data, 60, 3);
  Reading(GW_VAR_BSI_CLIMATE_CABIN_AIR_PURIFIER_STATE, (int32_t)raw, raw <= 5);
  raw = Bits(data, 57, 2);
  Reading(GW_VAR_BSI_CLIMATE_CABIN_AIR_QUALITY_LEVEL, (int32_t)raw, raw <= 2);
}

static uint32_t Packed24(const uint8_t * data)
{
  return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

// The capture confirms ASAP bit2 and feedback0/1 in 2E0's first byte.
// Preserve every program byte; RX never invokes control or transmits.
static void On2E0(const uint8_t * data)
{
  varstore_invalidate(GW_VAR_BSI_PRECOND_RX_GENERATION);
  unsigned available = data[0] >> 7, feedback = (data[0] >> 3) & 15U;
  varstore_write(GW_VAR_BSI_PRECOND_STATUS_RAW, data[0]);
  varstore_write(GW_VAR_BSI_PRECOND_AVAILABLE, available);
  varstore_write(GW_VAR_BSI_PRECOND_ASAP, (data[0] >> 2) & 1U);
  varstore_write(GW_VAR_BSI_PRECOND_FEEDBACK, feedback);
  Reading(GW_VAR_CLIMATE_STATE, feedback == 1, available && feedback <= 1);
  varstore_write(GW_VAR_BSI_PRECOND_PROGRAMS12_HI24, Packed24(data + 1));
  varstore_write(GW_VAR_BSI_PRECOND_PROGRAMS12_LO24, Packed24(data + 4));
  Received(GW_VAR_BSI_PRECOND_RX_GENERATION, &precond_generation);
}

static void On0E0(const uint8_t * data)
{
  varstore_invalidate(GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION);
  varstore_write(GW_VAR_BSI_PRECOND_PROGRAMS34_HI24, Packed24(data + 1));
  varstore_write(GW_VAR_BSI_PRECOND_PROGRAMS34_LO24, Packed24(data + 4));
  Received(GW_VAR_BSI_PRECOND_PROGRAMS34_RX_GENERATION, &programs34_generation);
}

int gw_can_info_decode(uint32_t id, const uint8_t * data, size_t length, bool extended, bool rtr)
{
  if (!data || extended || rtr || id > 0x7FF || length > 8)
    return -1;
  size_t expected;
  switch (id)
  {
    case 0x036: case 0x0B6: case 0x0F6: case 0x128:
    case 0x1E7: case 0x31A: case 0x3E0: case 0x2AD: case 0x350:
    case 0x236: case 0x276: case 0x39A: expected = 8; break;
    case 0x0A2: case 0x396: expected = 6; break;
    case 0x11C: expected = 4; break;
    case 0x160: expected = 2; break;
    case 0x2E0: case 0x0E0: case 0x261: case 0x2A1: expected = 7; break;
    default: return 0;
  }
  if (length != expected)
    return -1;
  switch (id)
  {
    case 0x036: On036(data); break;
    case 0x236: On236(data); break;
    case 0x276: On276(data); break;
    case 0x261: OnTrip(data, true); break;
    case 0x2A1: OnTrip(data, false); break;
    case 0x39A: On39A(data); break;
    case 0x2E0: On2E0(data); break;
    case 0x0E0: On0E0(data); break;
    case 0x160: On160(data); break;
    case 0x396: On396(data); break;
    case 0x2AD: On2AD(data); break;
    case 0x350: On350(data); break;
    case 0x0A2: On0A2(data); break;
    case 0x0B6: On0B6(data); break;
    case 0x0F6: On0F6(data); break;
    case 0x11C: On11C(data); break;
    case 0x128: On128(data); break;
    case 0x1E7: On1E7(data); break;
    case 0x31A: On31A(data); break;
    case 0x3E0:
      Reading(GW_VAR_SOC_PCT_X10, data[0] * 10, data[0] <= 100);
      Received(GW_VAR_SOC_RX_GENERATION, &soc_generation);
      break;
  }
  return 1;
}

void gw_can_info_init(void)
{
  bsi_generation = soc_generation = schedule_generation = 0;
  precond_generation = programs34_generation = 0;
  for (unsigned i = 0; i < NBI(button_presses); ++i) button_presses[i] = 0;
  gw_can_info_reset();
}
