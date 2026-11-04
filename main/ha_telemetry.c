// SPDX-License-Identifier: BSD-2-Clause
#include "process_data.h"
#include <stdio.h>
#include "ha_telemetry.h"
#include "cJSON.h"

typedef enum { NUMBER, BOOLEAN, ENUMERATION, LOCAL_TIME, AGE_SECONDS, PROGRAM } field_kind_t;
typedef struct {
    uint32_t var;
    const char *key, *name, *unit, *device_class;
    double divisor;
    int32_t lo, hi;
    field_kind_t kind;
    const char *const *options;
} field_t;

static const char *const climate_options[] = {"off", "on"};
static const char *const charge_options[] = {"disconnected", "waiting", "charging", "complete", "fault"};
static const char *const mode_options[] = {"immediate", "delayed"};
static const char *const bsi_options[] = {"standby", "normal", "going_to_sleep", "waking", "communication_off"};
static const char *const lock_options[] = {"invalid", "locked", "superlocked", "unlocked", "driver_unlocked", "cab_unlocked", "load_area_unlocked"};
static const char *const limit_options[] = {"disabled", "monitoring", "waiting_data", "busy", "requesting", "limit_reached", "retry", "unavailable", "low_aux_battery"};
static const field_t fields[] = {
    {GW_VAR_CHARGE_LIMIT_ENABLED,"charge_limit_enabled","Charge limit enabled",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_CHARGE_LIMIT_SOC_PCT_X10,"charge_limit_soc_pct","Charge limit target","%",NULL,10,10,1000,NUMBER,NULL},
    {GW_VAR_CHARGE_LIMIT_STATE,"charge_limit_state","Charge limit state",NULL,"enum",1,0,8,ENUMERATION,limit_options},
    {GW_VAR_BSI_AUX_BATTERY_SOC_PCT_X10,"aux_battery_soc_pct","12V battery state of charge","%","battery",10,0,1000,NUMBER,NULL},
    {GW_VAR_BSI_AUX_BATTERY_VOLTAGE_MV,"aux_battery_voltage_v","12V battery voltage","V","voltage",1000,0,32000,NUMBER,NULL},
    {GW_VAR_CLIMATE_LEFT_SETPOINT_MC,"climate_left_setpoint_c","Front left setpoint","°C","temperature",1000,0,60000,NUMBER,NULL},
    {GW_VAR_CLIMATE_RIGHT_SETPOINT_MC,"climate_right_setpoint_c","Front right setpoint","°C","temperature",1000,0,60000,NUMBER,NULL},
    {GW_VAR_ICSV_EVAPORATOR_MC,"evaporator_c","Evaporator temperature","°C","temperature",1000,-40000,85000,NUMBER,NULL},

    {GW_VAR_SOC_PCT_X10,"soc_pct","Battery state of charge","%","battery",10,0,1000,NUMBER,NULL},
    {GW_VAR_RANGE_KM_X10,"range_km","Estimated range","km","distance",10,0,20000,NUMBER,NULL},
    {GW_VAR_OUTSIDE_MC,"outside_c","Outside temperature","°C","temperature",1000,-60000,100000,NUMBER,NULL},
    {GW_VAR_ODOMETER_KM,"odometer_km","Odometer","km","distance",1,0,10000000,NUMBER,NULL},
    {GW_VAR_VEHICLE_AWAKE,"awake","Vehicle awake",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_VEHICLE_PARKED,"parked","Vehicle parked",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_CLIMATE_STATE,"climate_state","Reported preconditioning state",NULL,"enum",1,0,1,ENUMERATION,climate_options},
    {GW_VAR_CHARGE_STATE,"charge_state","Charging state",NULL,"enum",1,0,4,ENUMERATION,charge_options},
    {GW_VAR_CHARGE_MODE,"charge_mode","Charging mode",NULL,"enum",1,0,1,ENUMERATION,mode_options},
    {GW_VAR_CHARGE_CONNECTED,"charge_connected","Charging cable",NULL,"plug",1,0,1,BOOLEAN,NULL},
    {GW_VAR_CHARGE_START_MINUTE,"charge_start_local","Delayed charging start",NULL,NULL,1,0,1439,LOCAL_TIME,NULL},
    {GW_VAR_BSI_POWER_MANAGEMENT,"bsi_state","BSI state",NULL,"enum",1,0,4,ENUMERATION,bsi_options},
    {GW_VAR_SOC_PCT_X10,"vehicle_data_age_s","Battery data age","s","duration",1,0,1000,AGE_SECONDS,NULL},
    {GW_VAR_BSI_CHARGE_SCHEDULE_AVAILABLE,"charge_schedule_available","Charging schedule available",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_PRECOND_AVAILABLE,"preconditioning_available","Preconditioning available",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_PRECOND_ASAP,"preconditioning_requested","Immediate preconditioning accepted",NULL,NULL,1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_DOOR_DRIVER_OPEN,"door_driver_open","Driver door",NULL,"door",1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_DOOR_PASSENGER_OPEN,"door_passenger_open","Passenger door",NULL,"door",1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_DOOR_REAR_LEFT_OPEN,"door_rear_left_open","Rear left door",NULL,"door",1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_DOOR_REAR_RIGHT_OPEN,"door_rear_right_open","Rear right door",NULL,"door",1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_TRUNK_OPEN,"trunk_open","Boot",NULL,"door",1,0,1,BOOLEAN,NULL},
    {GW_VAR_BSI_LOCKING_STATE,"locking_state","Vehicle locking state",NULL,"enum",1,1,6,ENUMERATION,lock_options},
    {GW_VAR_BSI_TRIP1_AVERAGE_X10,"trip1_average","Trip 1 average consumption","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_TRIP2_AVERAGE_X10,"trip2_average","Trip 2 average consumption","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_TRIP_INSTANT_X10,"trip_instant","Instant consumption","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_TRIP1_DISTANCE_KM,"trip1_distance_km","Trip 1 distance","km","distance",1,0,65534,NUMBER,NULL},
    {GW_VAR_BSI_TRIP2_DISTANCE_KM,"trip2_distance_km","Trip 2 distance","km","distance",1,0,65534,NUMBER,NULL},
    {GW_VAR_BSI_TRIP1_SPEED_KMH,"trip1_speed_kmh","Trip 1 average speed","km/h","speed",1,0,254,NUMBER,NULL},
    {GW_VAR_BSI_TRIP2_SPEED_KMH,"trip2_speed_kmh","Trip 2 average speed","km/h","speed",1,0,254,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY1_X10,"consumption_history1","Consumption history 1","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY2_X10,"consumption_history2","Consumption history 2","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY3_X10,"consumption_history3","Consumption history 3","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY4_X10,"consumption_history4","Consumption history 4","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY5_X10,"consumption_history5","Consumption history 5","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_CONSUMPTION_HISTORY6_X10,"consumption_history6","Consumption history 6","kWh/100km",NULL,10,0,500,NUMBER,NULL},
    {GW_VAR_BSI_PRECOND_PROGRAMS12_HI24,"preconditioning_program1","Preconditioning program 1",NULL,NULL,1,0,0xFFFFFF,PROGRAM,NULL},
    {GW_VAR_BSI_PRECOND_PROGRAMS12_LO24,"preconditioning_program2","Preconditioning program 2",NULL,NULL,1,0,0xFFFFFF,PROGRAM,NULL},
    {GW_VAR_BSI_PRECOND_PROGRAMS34_HI24,"preconditioning_program3","Preconditioning program 3",NULL,NULL,1,0,0xFFFFFF,PROGRAM,NULL},
    {GW_VAR_BSI_PRECOND_PROGRAMS34_LO24,"preconditioning_program4","Preconditioning program 4",NULL,NULL,1,0,0xFFFFFF,PROGRAM,NULL},
};

// Retained discovery topics from older firmware must be deleted on reconnect.
static const struct { const char *component, *key; } retired[] = {
    {"binary_sensor", "demo"},
    {"sensor", "cabin_c"},
    {"sensor", "battery_min_c"},
    {"sensor", "battery_max_c"},
    {"sensor", "voltage_v"},
    {"sensor", "current_a"},
    {"sensor", "charge_power_kw"},
    {"sensor", "charge_remaining_min"},
};
size_t gw_ha_retired_count(void) { return sizeof(retired) / sizeof(retired[0]); }
const char *gw_ha_retired_key(size_t i) { return i < gw_ha_retired_count() ? retired[i].key : NULL; }
const char *gw_ha_retired_component(size_t i) { return i < gw_ha_retired_count() ? retired[i].component : NULL; }

size_t gw_ha_entity_count(void) { return (sizeof(fields) / sizeof(fields[0])); }
const char *gw_ha_entity_key(size_t i) { return i < (sizeof(fields) / sizeof(fields[0])) ? fields[i].key : NULL; }
const char *gw_ha_entity_component(size_t i)
{
    return i < (sizeof(fields) / sizeof(fields[0])) ? (fields[i].kind == BOOLEAN ? "binary_sensor" : "sensor") : NULL;
}

char *gw_ha_state_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *ages = cJSON_AddObjectToObject(root, "age_s");
    if (!ages) { cJSON_Delete(root); return NULL; }
    for (size_t i = 0; i < (sizeof(fields) / sizeof(fields[0])); ++i) {
        const field_t *f = &fields[i];
        // Match passive phone reads: retain last known values when CAN sleeps.
        // Field age is explicit; MQTT expiry describes a lost gateway link.
        varstore_snapshot_t sample = varstore_inspect(f->var, 0);
        bool valid = sample.state != VARSTORE_INVALID;
        int32_t value = sample.value;
        valid = valid && value >= f->lo && value <= f->hi;
        if (f->kind == LOCAL_TIME)
            valid = valid && varstore_check_valid(GW_VAR_CHARGE_MODE, 0);
        if (f->kind == PROGRAM) valid = valid && ((value & 2047) <= 1439 || (value & 2047) == 2047);
        uint32_t age = sample.state == VARSTORE_FORCED ? 0 : sample.age_ms;
        if (!(valid ? cJSON_AddNumberToObject(ages, f->key, age / 1000.0) : cJSON_AddNullToObject(ages, f->key))) {
            cJSON_Delete(root); return NULL;
        }
        cJSON *item;
        if (!valid) item = cJSON_CreateNull();
        else if (f->kind == AGE_SECONDS) item = cJSON_CreateNumber(age / 1000.0);
        else if (f->kind == PROGRAM) {
            char text[80];
            unsigned minute = value & 2047;
            if (minute == 2047) snprintf(text, sizeof(text), "Not configured");
            else {
                int length = snprintf(text, sizeof(text), "%02u:%02u %s", minute / 60, minute % 60, value & 0x800000 ? "on" : "off");
                static const char *const days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
                for (unsigned d = 0; d < 7; ++d) if (value & (1U << (22 - d)))
                    length += snprintf(text + length, sizeof(text) - length, " %s", days[d]);
            }
            item = cJSON_CreateString(text);
        }
        else if (f->kind == BOOLEAN) item = cJSON_CreateBool(value != 0);
        else if (f->kind == ENUMERATION) item = cJSON_CreateString(f->options[value]);
        else if (f->kind == LOCAL_TIME) {
            unsigned minutes = (unsigned)value;
            char time[6] = {(char)('0' + minutes / 600), (char)('0' + minutes / 60 % 10), ':',
                            (char)('0' + minutes % 60 / 10), (char)('0' + minutes % 10), 0};
            item = cJSON_CreateString(time);
        } else item = cJSON_CreateNumber(value / f->divisor);
        if (!item || !cJSON_AddItemToObject(root, f->key, item)) {
            cJSON_Delete(item); cJSON_Delete(root); return NULL;
        }
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *gw_ha_discovery_json(size_t i, const char *node, const char *state_topic,
                           const char *status_topic, const char *version,
                           const char *serial, unsigned expire_after_s)
{
    if (i >= (sizeof(fields) / sizeof(fields[0]))) return NULL;
    const field_t *f = &fields[i];
    char unique[96], value_template[128], available_template[128];
    snprintf(unique, sizeof(unique), "%s_%s", node, f->key);
    snprintf(value_template, sizeof(value_template), f->kind == BOOLEAN
        ? "{{ 'ON' if value_json.%s else 'OFF' }}" : "{{ value_json.%s }}", f->key);
    snprintf(available_template, sizeof(available_template),
        "{{ 'online' if value_json.%s is not none else 'offline' }}", f->key);
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    bool ok = cJSON_AddStringToObject(root,"name",f->name) &&
        cJSON_AddStringToObject(root,"unique_id",unique) &&
        cJSON_AddStringToObject(root,"state_topic",state_topic) &&
        cJSON_AddStringToObject(root,"value_template",value_template) &&
        cJSON_AddStringToObject(root,"availability_mode","all") &&
        cJSON_AddNumberToObject(root,"expire_after",expire_after_s);
    char attributes[240];
    snprintf(attributes, sizeof(attributes),
        "{{ {'age_s': value_json.age_s.%s, 'stale': (value_json.age_s.%s is not none and value_json.age_s.%s > 300)} | tojson }}",
        f->key, f->key, f->key);
    ok = ok && cJSON_AddStringToObject(root,"json_attributes_topic",state_topic) &&
        cJSON_AddStringToObject(root,"json_attributes_template",attributes);
    if (f->var == GW_VAR_BSI_AUX_BATTERY_VOLTAGE_MV)
        ok = ok && cJSON_AddNumberToObject(root,"suggested_display_precision",1);
    if (f->unit) ok = ok && cJSON_AddStringToObject(root,"unit_of_measurement",f->unit);
    if (f->device_class) ok = ok && cJSON_AddStringToObject(root,"device_class",f->device_class);
    if (f->kind == NUMBER || f->kind == AGE_SECONDS)
        ok = ok && cJSON_AddStringToObject(root,"state_class",f->var == GW_VAR_ODOMETER_KM ? "total_increasing" : "measurement");
    if (f->kind == BOOLEAN)
        ok = ok && cJSON_AddStringToObject(root,"payload_on","ON") && cJSON_AddStringToObject(root,"payload_off","OFF");
    cJSON *availability = ok ? cJSON_AddArrayToObject(root,"availability") : NULL;
    cJSON *device = availability ? cJSON_AddObjectToObject(root,"device") : NULL;
    cJSON *ids = device ? cJSON_AddArrayToObject(device,"identifiers") : NULL;
    ok = ok && ids && cJSON_AddItemToArray(ids,cJSON_CreateString(node)) &&
        cJSON_AddStringToObject(device,"name","eCMPilot gateway") &&
        cJSON_AddStringToObject(device,"manufacturer","Waveshare") &&
        cJSON_AddStringToObject(device,"model","ESP32-S3-RS485-CAN") &&
        cJSON_AddStringToObject(device,"sw_version",version) &&
        cJSON_AddStringToObject(device,"serial_number",serial);
    for (unsigned a = 0; ok && a < 2; ++a) {
        cJSON *item = cJSON_CreateObject();
        if (!item) { ok = false; break; }
        ok = cJSON_AddStringToObject(item,"topic",a ? state_topic : status_topic) &&
            (!a || cJSON_AddStringToObject(item,"value_template",available_template));
        if (ok) ok = cJSON_AddItemToArray(availability,item);
        if (!ok) cJSON_Delete(item);
    }
    if (ok && f->kind == ENUMERATION) {
        cJSON *options = cJSON_AddArrayToObject(root,"options");
        ok = options != NULL;
        for (int n = f->lo; ok && n <= f->hi; ++n) ok = cJSON_AddItemToArray(options,cJSON_CreateString(f->options[n]));
    }
    char *text = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return text;
}
