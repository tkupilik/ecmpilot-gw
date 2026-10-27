// SPDX-License-Identifier: BSD-2-Clause
#include "process_data.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const varstore_definition_t definitions[GW_VAR_COUNT] = {
    [GW_VAR_HEARTBEAT] = {"app.heartbeat", "Application heartbeat counter; producer CPU1", "count", 3000},
    [GW_VAR_UPTIME_S] = {"system.uptime_s", "System uptime; producer CPU1", "s", 3000},
    [GW_VAR_FREE_HEAP_BYTES] = {"system.free_heap_bytes", "Free heap memory; producer CPU1", "bytes", 3000},
    [GW_VAR_WIFI_CONNECTED] = {"wifi.connected", "Station connection state; producer CPU0", "bool", 3000},
    [GW_VAR_WIFI_RSSI_DBM] = {"wifi.rssi_dbm", "Station signal strength; invalid when offline; producer CPU0", "dBm", 3000},
    [GW_VAR_DEMO_VALUE] = {"demo.value", "Manual test signal; expires five seconds after Write or Touch", "raw", 5000},
    [GW_VAR_TEST_ENABLED] = {"test.enabled", "Varstore test controls: 0 disabled, 1 enabled; no CAN actions", "bool", 0},
    [GW_VAR_VEHICLE_AWAKE] = {"vehicle.awake", "Vehicle awake: 0 asleep, 1 awake", "bool", 5000},
    [GW_VAR_VEHICLE_PARKED] = {"vehicle.parked", "Vehicle parked: 0 moving, 1 parked", "bool", 5000},
    [GW_VAR_SOC_PCT_X10] = {"battery.soc_pct_x10", "Display SOC, tenths of a percent", "0.1 %", 5000},
    [GW_VAR_RANGE_KM_X10] = {"vehicle.range_km_x10", "Estimated range, tenths of a kilometre", "0.1 km", 60000},
    [GW_VAR_CABIN_C_X10] = {"temperature.cabin_c_x10", "Cabin temperature, tenths of a degree", "0.1 C", 5000},
    [GW_VAR_OUTSIDE_C_X10] = {"temperature.outside_c_x10", "Outside temperature, tenths of a degree", "0.1 C", 5000},
    [GW_VAR_BATTERY_MIN_C_X10] = {"battery.min_c_x10", "Minimum battery temperature, tenths of a degree", "0.1 C", 5000},
    [GW_VAR_BATTERY_MAX_C_X10] = {"battery.max_c_x10", "Maximum battery temperature, tenths of a degree", "0.1 C", 5000},
    [GW_VAR_ODOMETER_KM] = {"vehicle.odometer_km", "Vehicle odometer", "km", 60000},
    [GW_VAR_VOLTAGE_V_X10] = {"battery.voltage_v_x10", "HV battery voltage, tenths of a volt", "0.1 V", 5000},
    [GW_VAR_CURRENT_A_X10] = {"battery.current_a_x10", "HV current; positive discharge, negative charging", "0.1 A", 5000},
    [GW_VAR_CLIMATE_STATE] = {"climate.state", "Climate state: 0 off, 1 on", "enum", 5000},
    [GW_VAR_CHARGE_STATE] = {"charge.state", "Charge state: 0 disconnected, 1 waiting, 2 charging, 3 complete, 4 fault", "enum", 5000},
    [GW_VAR_CHARGE_MODE] = {"charge.mode", "Charge mode: 0 immediate, 1 deferred", "enum", 5000},
    [GW_VAR_CHARGE_CONNECTED] = {"charge.connected", "Cable connected: 0 no, 1 yes", "bool", 5000},
    [GW_VAR_CHARGE_POWER_W] = {"charge.power_w", "Net power into HV battery, not wall input", "W", 5000},
    [GW_VAR_CHARGE_REMAINING_MIN] = {"charge.remaining_min", "Vehicle-reported remaining charge time", "min", 5000},
    [GW_VAR_CHARGE_START_MINUTE] = {"charge.start_minute", "Deferred start, minutes after midnight; test timezone Europe/Prague", "min", 5000},
    [GW_VAR_REQUEST_CLIMATE_ENABLED] = {"request.climate.enabled", "Last accepted climate target; producer UDP CPU0", "bool", 30000},
    [GW_VAR_REQUEST_CLIMATE_GENERATION] = {"request.climate.generation", "Climate acceptance generation; published after target", "count", 0},
    [GW_VAR_REQUEST_CHARGE_MODE] = {"request.charge.mode", "Last accepted charge mode: 0 immediate, 1 deferred", "enum", 30000},
    [GW_VAR_REQUEST_CHARGE_START_MINUTE] = {"request.charge.start_minute", "Last deferred target; invalid for immediate mode", "min", 30000},
    [GW_VAR_REQUEST_CHARGE_GENERATION] = {"request.charge.generation", "Charge acceptance generation; published after target", "count", 0},
    [GW_VAR_UDP_RX_COUNT] = {"udp.rx_count", "Received UDP datagrams; producer UDP CPU0", "count", 0},
    [GW_VAR_UDP_TX_COUNT] = {"udp.tx_count", "Sent UDP replies; producer UDP CPU0", "count", 0},
    [GW_VAR_UDP_DROP_COUNT] = {"udp.drop_count", "Malformed, oversized, limited or unsent UDP datagrams", "count", 0},
    [GW_VAR_UDP_COMMAND_COUNT] = {"udp.command_count", "Accepted commands; retries do not increment", "count", 0},
};
static DRAM_ATTR varstore_slot_t slots[GW_VAR_COUNT];

static uint64_t clock_ms(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time() / 1000;
}
esp_err_t gw_process_data_init(void)
{
    varstore_init(definitions, slots, GW_VAR_COUNT, clock_ms, NULL);
    return varstore_count() == GW_VAR_COUNT ? ESP_OK : ESP_ERR_INVALID_STATE;
}
bool gw_process_data_manual(size_t id) { return id == GW_VAR_DEMO_VALUE || (id >= GW_VAR_TEST_ENABLED && id <= GW_VAR_CHARGE_START_MINUTE); }
