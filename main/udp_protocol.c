// SPDX-License-Identifier: BSD-2-Clause
#include "udp_protocol.h"
#include "process_data.h"
#include "vehicle_control.h"
#include "auto_refresh.h"
#include "low_power.h"
#include "cJSON.h"
#include "esp_log.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RETENTION_MS 600000
#define CONFIRM_MS 30000
#define MAX_SAFE_INTEGER 9007199254740991.0
typedef enum { EMPTY, ACCEPTED, SUCCEEDED, FAILED, UNKNOWN, REJECTED } phase_t;
typedef struct {
    char client[33], id[33];
    uint64_t created, terminal_at, deadline;
    uint32_t supplied_rev, domain_rev, revision;
    int target, minute;
    unsigned domain;
    uint32_t control_generation;
    uint32_t correlation_id;
    char client_session[33];
    bool physical;
    bool programs;
    uint32_t program_base[4], program_target[4];
    phase_t phase;
    const char *error;
    const char *reason;
} command_t;
static command_t commands[GW_UDP_COMMAND_CAPACITY];
static char gateway_id[81], boot_id[33], firmware_version[81];
static uint64_t sequence;
static uint64_t overload_until;
static uint32_t generations[3], accepted_count, control_requests;

static const cJSON *get(const cJSON *j, const char *key) { return cJSON_GetObjectItemCaseSensitive(j, key); }
static bool text_is(const cJSON *j, const char *value) { return cJSON_IsString(j) && !strcmp(j->valuestring, value); }
static bool number(const cJSON *j, double lo, double hi)
{
    return cJSON_IsNumber(j) && isfinite(j->valuedouble) && j->valuedouble >= lo &&
           j->valuedouble <= hi && floor(j->valuedouble) == j->valuedouble;
}
static bool id_valid(const cJSON *j)
{
    if (!cJSON_IsString(j) || strlen(j->valuestring) != 32) return false;
    for (const char *p = j->valuestring; *p; ++p)
        if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) return false;
    return true;
}
static bool keys(const cJSON *j, const char *const *allowed, size_t count)
{
    if (!cJSON_IsObject(j)) return false;
    for (const cJSON *p = j->child; p; p = p->next) {
        bool found = false;
        for (size_t i = 0; i < count; ++i) if (!strcmp(p->string, allowed[i])) found = true;
        if (!found) return false;
    }
    return true;
}
// The request vocabulary is ASCII, including IDs, operation names and timezone.
// Reject NULs/escapes to NUL, duplicate keys and pathological nesting before use.
static bool tree_valid(const cJSON *j, unsigned depth)
{
    if (depth > 6) return false;
    if (cJSON_IsNumber(j) && !isfinite(j->valuedouble)) return false;
    for (const cJSON *p = j->child; p; p = p->next) {
        if (cJSON_IsObject(j)) {
            for (const cJSON *q = j->child; q != p; q = q->next)
                if (!strcmp(q->string, p->string)) return false;
        }
        if (!tree_valid(p, depth + 1)) return false;
    }
    return true;
}
static bool input_valid(const char *data, size_t length)
{
    // All supported request strings use ASCII. A future non-ASCII argument
    // needs explicit UTF-8 validation when its operation is introduced.
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (!c || c >= 128 || (c < 32 && c != '\r' && c != '\n' && c != '\t')) return false;
        if (quoted) {
            if (c < 32) return false;
            if (c == '\\') {
                if (i + 5 < length && !memcmp(data + i, "\\u0000", 6)) return false;
                if (++i >= length) return false;
            } else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') { quoted = true; continue; }
        if (c == '{' || c == '[') { if (++depth > 6) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t end = i;
            if (data[end] == '-') ++end;
            if (end >= length || data[end] < '0' || data[end] > '9') return false;
            if (data[end] == '0') ++end;
            else while (end < length && data[end] >= '0' && data[end] <= '9') ++end;
            if (end < length && data[end] == '.') {
                size_t first = ++end;
                while (end < length && data[end] >= '0' && data[end] <= '9') ++end;
                if (end == first) return false;
            }
            if (end < length && (data[end] == 'e' || data[end] == 'E')) {
                ++end;
                if (end < length && (data[end] == '+' || data[end] == '-')) ++end;
                size_t first = end;
                while (end < length && data[end] >= '0' && data[end] <= '9') ++end;
                if (end == first) return false;
            }
            if (end < length && !strchr(" \t\r\n,]}", data[end])) return false;
            i = end - 1;
        }
    }
    return !quoted && !depth;
}
static bool test_enabled(void) { return varstore_read(GW_VAR_TEST_ENABLED, 0, 0) == 1; }
static varstore_snapshot_t sample(size_t id)
{
    varstore_snapshot_t s = varstore_inspect(id, 0);
    // An override is a continuously valid effective observation. Keep its
    // actual force age in the web diagnostics, not in consumer freshness.
    if (s.state == VARSTORE_FORCED) s.age_ms = 0;
    return s;
}
static bool known(varstore_snapshot_t s, int32_t lo, int32_t hi)
{
    return s.state != VARSTORE_INVALID && s.value >= lo && s.value <= hi;
}
static bool fresh(size_t id, int32_t lo, int32_t hi, int32_t *out)
{
    varstore_snapshot_t s = sample(id);
    if (!known(s, lo, hi) || s.age_ms > 5000) return false;
    *out = s.value;
    return true;
}
static void finish(command_t *c, phase_t phase, const char *error, uint64_t now)
{
    c->phase = phase; c->error = error; c->terminal_at = now; ++c->revision;
    if (c->physical) varstore_write(GW_VAR_CONTROL_RECEIPT_GENERATION, c->control_generation);
}
static int32_t producer(size_t id, int32_t fallback)
{
    varstore_snapshot_t s = varstore_inspect(id, 0);
    return s.raw_valid ? s.raw_value : fallback;
}
static const char *control_error(void)
{
    switch (producer(GW_VAR_CONTROL_RESULT, 0)) {
    case GW_CONTROL_RESULT_WAKE_TIMEOUT: return "bsi_wake_timeout";
    case GW_CONTROL_RESULT_TX_TIMEOUT: return "can_tx_timeout";
    case GW_CONTROL_RESULT_TX_FAILED: return "can_tx_failed";
    case GW_CONTROL_RESULT_PRECOND_UNAVAILABLE: return "preconditioning_unavailable";
    case GW_CONTROL_RESULT_PRECOND_REJECTED: return "preconditioning_rejected";
    case GW_CONTROL_RESULT_PROGRAMS_TIMEOUT: return "preconditioning_programs_timeout";
    case GW_CONTROL_RESULT_PROGRAMS_CONFLICT: return "preconditioning_programs_conflict";
    case GW_CONTROL_RESULT_REPLY_TIMEOUT: return "vehicle_reply_timeout";
    case GW_CONTROL_RESULT_OPERATION_TIMEOUT: return "operation_timeout";
    case GW_CONTROL_RESULT_CONFIG: return "invalid_wake_config";
    case GW_CONTROL_RESULT_MOVING: return "vehicle_moving";
    case GW_CONTROL_RESULT_UNAVAILABLE: return "vehicle_control_unavailable";
    default: return "unconfirmed";
    }
}
void gw_udp_protocol_tick(uint64_t now)
{
    for (size_t i = 0; i < GW_UDP_COMMAND_CAPACITY; ++i) {
        command_t *c = &commands[i];
        if (c->phase == EMPTY) continue;
        if (c->phase != ACCEPTED) {
            if (now - c->terminal_at >= RETENTION_MS) c->phase = EMPTY;
            continue;
        }
        if (c->physical) {
            if ((uint32_t)producer(GW_VAR_CONTROL_GENERATION, 0) == c->control_generation) {
                int phase = producer(GW_VAR_CONTROL_PHASE, 0);
                if (phase == GW_CONTROL_SUCCEEDED) finish(c, SUCCEEDED, NULL, now);
                else if (phase == GW_CONTROL_FAILED) finish(c, FAILED, control_error(), now);
                else if (phase == GW_CONTROL_UNCONFIRMED) finish(c, UNKNOWN, control_error(), now);
            }
            if (c->phase == ACCEPTED && now - c->created >= 35000) finish(c, UNKNOWN, "unconfirmed", now);
            continue;
        }
        if (!test_enabled()) { finish(c, FAILED, "test_mode_disabled", now); continue; }
        if (c->domain == 2) { finish(c, SUCCEEDED, NULL, now); continue; }
        if (now - c->created >= CONFIRM_MS) { finish(c, UNKNOWN, "unconfirmed", now); continue; }
        // Do not accept an observation older than this command as readback.
        // Forced values are deliberately current effective observations.
        size_t id = c->domain ? GW_VAR_CHARGE_MODE : GW_VAR_CLIMATE_STATE;
        varstore_snapshot_t s = sample(id);
        bool confirmed = known(s, 0, 1) && s.value == c->target && s.age_ms <= 5000 && s.age_ms <= now - c->created;
        if (confirmed && c->domain && c->minute >= 0) {
            varstore_snapshot_t start = sample(GW_VAR_CHARGE_START_MINUTE);
            confirmed = known(start, 0, 1439) && start.value == c->minute && start.age_ms <= 5000 && start.age_ms <= now - c->created;
        }
        if (confirmed) finish(c, SUCCEEDED, NULL, now);
    }
}
void gw_udp_protocol_init(const char *gateway, const char *boot, const char *firmware)
{
    snprintf(gateway_id, sizeof(gateway_id), "%s", gateway);
    snprintf(boot_id, sizeof(boot_id), "%s", boot);
    snprintf(firmware_version, sizeof(firmware_version), "%s", firmware);
    memset(commands, 0, sizeof(commands));
    generations[0] = generations[1] = generations[2] = accepted_count = control_requests = 0; sequence = 0;
    overload_until = 0;
    varstore_write(GW_VAR_REQUEST_VEHICLE_GENERATION, 0);
    varstore_write(GW_VAR_CONTROL_RECEIPT_GENERATION, 0);
    varstore_write(GW_VAR_REQUEST_CLIMATE_GENERATION, 0);
    varstore_write(GW_VAR_REQUEST_CHARGE_GENERATION, 0);
    varstore_write(GW_VAR_UDP_COMMAND_COUNT, 0);
}
static cJSON *result(const char *id, uint32_t revision, uint32_t generation, const char *phase, const char *error)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "id", id); cJSON_AddNumberToObject(r, "revision", revision);
    cJSON_AddNumberToObject(r, "domain_rev", generation); cJSON_AddStringToObject(r, "phase", phase);
    if (error) cJSON_AddStringToObject(r, "error", error); else cJSON_AddNullToObject(r, "error");
    return r;
}
static cJSON *receipt(const command_t *c)
{
    static const char *const phases[] = {"unknown", "accepted", "succeeded", "failed", "unknown", "rejected"};
    cJSON *r = result(c->id, c->revision, c->domain_rev, phases[c->phase], c->error);
    if (c->reason) cJSON_AddStringToObject(r, "reason", c->reason);
    if (c->correlation_id) {
        cJSON_AddNumberToObject(r, "correlation_id", c->correlation_id);
        cJSON_AddStringToObject(r, "client_session", c->client_session);
    }
    return r;
}
static void reject(cJSON *response, const char *id, bool write, const char *error, const char *reason)
{
    if (!write) { cJSON_AddStringToObject(response, "error", error); return; }
    cJSON *r = result(id, 1, 0, "rejected", error);
    if (reason) cJSON_AddStringToObject(r, "reason", reason);
    cJSON_AddItemToObject(response, "command", r);
}
typedef struct { const char *name; size_t id; double scale; int32_t lo, hi; } metric_t;
static const metric_t metrics[] = {
    {"soc_pct", GW_VAR_SOC_PCT_X10, 10, 0, 1000},
    {"range_km", GW_VAR_RANGE_KM_X10, 10, 0, 20000},
    {"cabin_c", GW_VAR_CABIN_MC, 1000, -60000, 100000},
    {"outside_c", GW_VAR_OUTSIDE_MC, 1000, -60000, 100000},
    {"battery_min_c", GW_VAR_BATTERY_MIN_MC, 1000, -60000, 150000},
    {"battery_max_c", GW_VAR_BATTERY_MAX_MC, 1000, -60000, 150000},
    {"odometer_km", GW_VAR_ODOMETER_KM, 1, 0, 10000000},
    {"climate_left_setpoint_c", GW_VAR_CLIMATE_LEFT_SETPOINT_MC, 1000, 0, 60000},
    {"climate_right_setpoint_c", GW_VAR_CLIMATE_RIGHT_SETPOINT_MC, 1000, 0, 60000},
    {"evaporator_c", GW_VAR_ICSV_EVAPORATOR_MC, 1000, -40000, 85000},
    {"aux_battery_soc_pct", GW_VAR_BSI_AUX_BATTERY_SOC_PCT_X10, 10, 0, 1000},
    {"aux_battery_voltage_v", GW_VAR_BSI_AUX_BATTERY_VOLTAGE_MV, 1000, 0, 32000},
    {"voltage_v", GW_VAR_VOLTAGE_MV, 1000, 0, 1000000},
    {"current_a", GW_VAR_CURRENT_MA, 1000, -2000000, 2000000},
};
static cJSON *info(void)
{
    cJSON *j = cJSON_CreateObject(), *fields = cJSON_AddArrayToObject(j, "fields");
    cJSON_AddStringToObject(j, "firmware", firmware_version);
    cJSON_AddStringToObject(j, "security", "lab-plain");
    const bool test = test_enabled();
    cJSON_AddStringToObject(j, "control_backend", test ? "varstore-test" : gw_vehicle_control_available() ? "info-can-experimental" : "none");
    if (test || gw_vehicle_control_available()) cJSON_AddStringToObject(j, "timezone", "Europe/Prague"); else cJSON_AddNullToObject(j, "timezone");
    for (size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i)
        cJSON_AddItemToArray(fields, cJSON_CreateString(metrics[i].name));
    cJSON_AddItemToArray(fields, cJSON_CreateString("vehicle_details"));
    cJSON_AddItemToArray(fields, cJSON_CreateString("climate_programs"));
    cJSON_AddItemToArray(fields, cJSON_CreateString("auto_refresh"));
    cJSON_AddItemToArray(fields, cJSON_CreateString("low_power"));
    cJSON_AddItemToArray(fields, cJSON_CreateString("charge_limit"));
    cJSON *operations = cJSON_AddObjectToObject(j, "operations");
    if (!test && gw_vehicle_control_available()) {
        cJSON *programs = cJSON_AddObjectToObject(operations, "climate.programs.set");
        cJSON_AddBoolToObject(programs, "verified", false);
        cJSON_AddBoolToObject(programs, "confirmation", true);
        cJSON_AddBoolToObject(programs, "persistent_schedule", true);
    }
    if (test || gw_vehicle_control_available()) {
        cJSON *climate = cJSON_AddObjectToObject(operations, "climate.set");
        cJSON_AddBoolToObject(climate, "verified", false);
        cJSON_AddBoolToObject(climate, "confirmation", true);
        cJSON *values = cJSON_AddArrayToObject(climate, "enabled");
        cJSON_AddItemToArray(values, cJSON_CreateBool(true)); cJSON_AddItemToArray(values, cJSON_CreateBool(false));
    }
    if (test || gw_vehicle_control_available()) {
        cJSON *refresh = cJSON_AddObjectToObject(operations, "vehicle.refresh");
        cJSON_AddBoolToObject(refresh, "verified", false);
        cJSON_AddBoolToObject(refresh, "confirmation", true);
        cJSON *charge = cJSON_AddObjectToObject(operations, "charge.mode.set");
        cJSON_AddBoolToObject(charge, "verified", false); cJSON_AddBoolToObject(charge, "confirmation", true);
        cJSON_AddBoolToObject(charge, "persistent_schedule", !test);
        cJSON *modes = cJSON_AddArrayToObject(charge, "modes");
        cJSON_AddItemToArray(modes, cJSON_CreateString("immediate")); cJSON_AddItemToArray(modes, cJSON_CreateString("deferred"));
    }
    return j;
}
static void put_age(cJSON *j, bool valid, uint32_t age)
{
    if (valid) cJSON_AddNumberToObject(j, "age_ms", age); else cJSON_AddNullToObject(j, "age_ms");
}
static uint32_t older(uint32_t a, uint32_t b) { return a > b ? a : b; }
static void detail_reading(cJSON *j, const char *name, size_t id, int32_t maximum, double scale)
{
    varstore_snapshot_t s = sample(id);
    bool valid = known(s, 0, maximum);
    cJSON *r = cJSON_AddObjectToObject(j, name);
    if (valid) cJSON_AddNumberToObject(r, "value", s.value / scale);
    else cJSON_AddNullToObject(r, "value");
    put_age(r, valid, s.age_ms);
}
// A separate passive snapshot preserves the v0 1024-byte state envelope.
static cJSON *details(void)
{
    cJSON *j = cJSON_CreateObject(), *body = cJSON_AddObjectToObject(j, "body");
    static const size_t doors[] = {GW_VAR_BSI_DOOR_DRIVER_OPEN, GW_VAR_BSI_DOOR_PASSENGER_OPEN,
        GW_VAR_BSI_DOOR_REAR_LEFT_OPEN, GW_VAR_BSI_DOOR_REAR_RIGHT_OPEN, GW_VAR_BSI_TRUNK_OPEN};
    unsigned mask = 0, valid_mask = 0; uint32_t age = 0;
    for (unsigned i = 0; i < sizeof(doors) / sizeof(doors[0]); ++i) {
        varstore_snapshot_t s = sample(doors[i]);
        if (!known(s, 0, 1)) continue;
        valid_mask |= 1U << i;
        if (s.value) mask |= 1U << i;
        age = older(age, s.age_ms);
    }
    cJSON_AddNumberToObject(body, "open_mask", mask);
    cJSON_AddNumberToObject(body, "known_mask", valid_mask);
    put_age(body, valid_mask != 0, age);
    varstore_snapshot_t lock = sample(GW_VAR_BSI_LOCKING_STATE);
    if (known(lock, 1, 6)) {
        cJSON_AddNumberToObject(body, "locking_state", lock.value);
        cJSON_AddNumberToObject(body, "lock_age_ms", lock.age_ms);
    } else {
        cJSON_AddNullToObject(body, "locking_state"); cJSON_AddNullToObject(body, "lock_age_ms");
    }
    cJSON *trips = cJSON_AddObjectToObject(j, "trips");
    detail_reading(trips, "avg1", GW_VAR_BSI_TRIP1_AVERAGE_X10, 500, 10);
    detail_reading(trips, "avg2", GW_VAR_BSI_TRIP2_AVERAGE_X10, 500, 10);
    detail_reading(trips, "instant", GW_VAR_BSI_TRIP_INSTANT_X10, 500, 10);
    detail_reading(trips, "distance1", GW_VAR_BSI_TRIP1_DISTANCE_KM, 65534, 1);
    detail_reading(trips, "distance2", GW_VAR_BSI_TRIP2_DISTANCE_KM, 65534, 1);
    detail_reading(trips, "speed1", GW_VAR_BSI_TRIP1_SPEED_KMH, 254, 1);
    detail_reading(trips, "speed2", GW_VAR_BSI_TRIP2_SPEED_KMH, 254, 1);
    cJSON *history = cJSON_AddArrayToObject(trips, "history");
    age = 0; bool any = false;
    for (unsigned i = 0; i < 6; ++i) {
        varstore_snapshot_t s = sample(GW_VAR_BSI_CONSUMPTION_HISTORY1_X10 + i);
        if (known(s, 0, 500)) {
            cJSON_AddItemToArray(history, cJSON_CreateNumber(s.value / 10.0));
            age = older(age, s.age_ms); any = true;
        } else cJSON_AddItemToArray(history, cJSON_CreateNull());
    }
    cJSON_AddNumberToObject(trips, "history_age_ms", any ? age : 0);
    if (!any) { cJSON_DeleteItemFromObjectCaseSensitive(trips, "history_age_ms"); cJSON_AddNullToObject(trips, "history_age_ms"); }
    return j;
}
static cJSON *programs(void)
{
    static const size_t ids[] = {GW_VAR_BSI_PRECOND_PROGRAMS12_HI24, GW_VAR_BSI_PRECOND_PROGRAMS12_LO24,
        GW_VAR_BSI_PRECOND_PROGRAMS34_HI24, GW_VAR_BSI_PRECOND_PROGRAMS34_LO24};
    cJSON *j = cJSON_CreateObject(), *slots = cJSON_AddArrayToObject(j, "slots");
    cJSON_AddNumberToObject(j, "rev", generations[0]);
    for (unsigned i = 0; i < 4; ++i) {
        varstore_snapshot_t s = sample(ids[i]);
        cJSON *slot = cJSON_CreateObject(); cJSON_AddItemToArray(slots, slot);
        if (known(s, 0, 0xFFFFFF)) cJSON_AddNumberToObject(slot, "raw", s.value);
        else cJSON_AddNullToObject(slot, "raw");
        put_age(slot, known(s, 0, 0xFFFFFF), s.age_ms);
    }
    return j;
}
static void boolean(cJSON *j, const char *name, varstore_snapshot_t s)
{
    if (known(s, 0, 1)) cJSON_AddBoolToObject(j, name, s.value); else cJSON_AddNullToObject(j, name);
}
static cJSON *state(void)
{
    cJSON *j = cJSON_CreateObject();
    for (size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i) {
        const metric_t *m = &metrics[i]; varstore_snapshot_t s = sample(m->id);
        cJSON *reading = cJSON_AddObjectToObject(j, m->name);
        const bool valid = known(s, m->lo, m->hi);
        if (valid) cJSON_AddNumberToObject(reading, "value", s.value / m->scale); else cJSON_AddNullToObject(reading, "value");
        put_age(reading, valid, s.age_ms);
    }
    varstore_snapshot_t awake = sample(GW_VAR_VEHICLE_AWAKE), parked = sample(GW_VAR_VEHICLE_PARKED);
    cJSON *v = cJSON_AddObjectToObject(j, "vehicle"); boolean(v, "awake", awake); boolean(v, "parked", parked);
    put_age(v, known(awake, 0, 1) && known(parked, 0, 1), older(awake.age_ms, parked.age_ms));
    varstore_snapshot_t pm = sample(GW_VAR_BSI_POWER_MANAGEMENT);
    if (known(pm, 0, 4)) {
        cJSON *bsi = cJSON_AddObjectToObject(j, "bsi");
        cJSON_AddNumberToObject(bsi, "power_management", pm.value);
        put_age(bsi, true, pm.age_ms);
    }
    varstore_snapshot_t climate = sample(GW_VAR_CLIMATE_STATE);
    cJSON *c = cJSON_AddObjectToObject(j, "climate");
    cJSON_AddStringToObject(c, "state", known(climate, 0, 1) ? (climate.value ? "on" : "off") : "unknown");
    cJSON_AddNumberToObject(c, "rev", generations[0]); put_age(c, known(climate, 0, 1), climate.age_ms);
    varstore_snapshot_t status = sample(GW_VAR_CHARGE_STATE), mode = sample(GW_VAR_CHARGE_MODE), connected = sample(GW_VAR_CHARGE_CONNECTED);
    static const char *const charge_states[] = {"disconnected", "waiting", "charging", "complete", "fault"};
    cJSON *q = cJSON_AddObjectToObject(j, "charge");
    cJSON_AddStringToObject(q, "state", known(status, 0, 4) ? charge_states[status.value] : "unknown");
    cJSON_AddStringToObject(q, "mode", known(mode, 0, 1) ? (mode.value ? "deferred" : "immediate") : "unknown");
    cJSON_AddNumberToObject(q, "rev", generations[1]); boolean(q, "connected", connected);
    bool valid = known(status, 0, 4) && known(mode, 0, 1) && known(connected, 0, 1);
    uint32_t age = older(status.age_ms, older(mode.age_ms, connected.age_ms));
    varstore_snapshot_t power = sample(GW_VAR_CHARGE_POWER_W), remaining = sample(GW_VAR_CHARGE_REMAINING_MIN);
    if (known(power, 0, 1000000)) { cJSON_AddNumberToObject(q, "power_kw", power.value / 1000.0); age = older(age, power.age_ms); }
    else cJSON_AddNullToObject(q, "power_kw");
    if (known(remaining, 0, 100000)) { cJSON_AddNumberToObject(q, "remaining_min", remaining.value); age = older(age, remaining.age_ms); }
    else cJSON_AddNullToObject(q, "remaining_min");
    if (known(mode, 0, 1)) cJSON_AddNumberToObject(q, "mode_age_ms", mode.age_ms);
    varstore_snapshot_t available = sample(GW_VAR_BSI_CHARGE_SCHEDULE_AVAILABLE);
    if (known(available, 0, 1)) boolean(q, "schedule_available", available);
    if (known(mode, 0, 1)) {
        varstore_snapshot_t start = sample(GW_VAR_CHARGE_START_MINUTE);
        if (known(start, 0, 1439)) {
            const unsigned minutes = (unsigned)start.value;
            char time[6] = {(char)('0' + minutes / 600), (char)('0' + minutes / 60 % 10), ':',
                            (char)('0' + minutes % 60 / 10), (char)('0' + minutes % 10), 0};
            cJSON_AddStringToObject(q, "start_local", time); age = older(age, start.age_ms);
        } else { cJSON_AddNullToObject(q, "start_local"); if (mode.value) valid = false; }
    }
    put_age(q, valid, age);
    return j;
}
static command_t *find(const char *client, const char *id)
{
    for (size_t i = 0; i < GW_UDP_COMMAND_CAPACITY; ++i)
        if (commands[i].phase != EMPTY && !strcmp(commands[i].client, client) && !strcmp(commands[i].id, id)) return &commands[i];
    return NULL;
}
static command_t *find_correlation(const char *client, const char *session, uint32_t correlation)
{
    if (!correlation) return NULL;
    for (size_t i = 0; i < GW_UDP_COMMAND_CAPACITY; ++i) {
        command_t *c = &commands[i];
        if (c->phase != EMPTY && c->correlation_id == correlation &&
            !strcmp(c->client, client) && !strcmp(c->client_session, session)) return c;
    }
    return NULL;
}
static uint32_t session_word(const char *session, unsigned word)
{
    uint32_t value = 0;
    for (unsigned i = word * 8; i < word * 8 + 8; ++i) {
        char ch = session[i]; value = (value << 4) | (uint32_t)(ch <= '9' ? ch - '0' : ch - 'a' + 10);
    }
    return value;
}
static const char *prerequisite(unsigned domain)
{
    int32_t value;
    if (!fresh(GW_VAR_VEHICLE_AWAKE, 0, 1, &value) || !value) return "vehicle_unavailable";
    if (!fresh(GW_VAR_VEHICLE_PARKED, 0, 1, &value) || !value) return "not_parked";
    if (domain && (!fresh(GW_VAR_CHARGE_CONNECTED, 0, 1, &value) || !value)) return "cable_disconnected";
    return NULL;
}
static bool parse_time(const cJSON *time, int *minute)
{
    if (!cJSON_IsString(time) || strlen(time->valuestring) != 5) return false;
    const char *s = time->valuestring;
    if (s[2] != ':' || s[0] < '0' || s[0] > '2' || s[1] < '0' || s[1] > '9' ||
        s[3] < '0' || s[3] > '5' || s[4] < '0' || s[4] > '9') return false;
    int hours = (s[0] - '0') * 10 + s[1] - '0';
    *minute = hours * 60 + (s[3] - '0') * 10 + s[4] - '0';
    return hours < 24;
}
static void write_request(const cJSON *r, cJSON *reply, const char *id, const char *client, unsigned domain, uint64_t now)
{
    const cJSON *args = get(r, "args"), *deadline = get(r, "deadline_ms"), *rev = get(r, "domain_rev");
    static const char *const root_keys[] = {"v","type","gw","boot","client","id","deadline_ms","domain_rev","op","args","client_session","correlation_id"};
    const cJSON *correlation = get(r, "correlation_id"), *session = get(r, "client_session");
    uint32_t correlation_id = correlation ? (uint32_t)correlation->valuedouble : 0;
    const char *client_session = session ? session->valuestring : "";
    static const char *const climate_keys[] = {"enabled"};
    static const char *const charge_keys[] = {"mode","start_local","timezone"};
    static const char *const program_keys[] = {"base","slots"};
    bool edit_programs = text_is(get(r, "op"), "climate.programs.set");
    uint32_t base[4] = {0}, targets[4] = {0};
    int target = 0, minute = -1;
    const char *error = NULL;
    if (!keys(r, root_keys, sizeof(root_keys) / sizeof(root_keys[0]))) error = "unsupported_argument";
    else if (edit_programs) {
        const cJSON *a = get(args, "base"), *b = get(args, "slots");
        if (!keys(args, program_keys, 2) || !cJSON_IsArray(a) || !cJSON_IsArray(b) ||
            cJSON_GetArraySize(a) != 4 || cJSON_GetArraySize(b) != 4) error = "invalid_argument";
        else for (unsigned i = 0; i < 4; ++i) {
            const cJSON *av = cJSON_GetArrayItem(a, i), *bv = cJSON_GetArrayItem(b, i);
            if (!number(av, 0, 0xFFFFFF) || !number(bv, 0, 0xFFFFFF)) { error = "invalid_argument"; break; }
            base[i] = (uint32_t)av->valuedouble; targets[i] = (uint32_t)bv->valuedouble;
            if (((base[i] ^ targets[i]) & 0xF800) ||
                ((targets[i] & 0x800000) && (!(targets[i] & 0x7F0000) || (targets[i] & 0x7FF) > 1439))) {
                error = "invalid_argument"; break;
            }
        }
    } else if (domain == 2) {
        if (args->child) error = "unsupported_argument";
    } else if (!domain) {
        if (!keys(args, climate_keys, 1) || !cJSON_IsBool(get(args, "enabled"))) error = "unsupported_argument";
        else target = cJSON_IsTrue(get(args, "enabled"));
    } else if (text_is(get(args, "mode"), "immediate")) {
        if (!keys(args, charge_keys, 3)) error = "unsupported_argument";
        else if ((get(args, "start_local") || get(args, "timezone")) &&
            (!parse_time(get(args, "start_local"), &minute) || !text_is(get(args, "timezone"), "Europe/Prague"))) error = "invalid_argument";
    } else if (text_is(get(args, "mode"), "deferred")) {
        target = 1;
        if (!keys(args, charge_keys, 3)) error = "unsupported_argument";
        else if (!parse_time(get(args, "start_local"), &minute) || !text_is(get(args, "timezone"), "Europe/Prague")) error = "invalid_argument";
    } else error = "invalid_argument";
    if (!error && (!number(deadline, 0, MAX_SAFE_INTEGER) || !number(rev, 0, INT32_MAX))) error = "invalid_argument";
    if (error) { reject(reply, id, true, error, NULL); return; }
    command_t *old = find(client, id);
    if (!old) old = find_correlation(client, client_session, correlation_id);
    if (old) {
        if (!text_is(get(r, "boot"), boot_id) || old->deadline != (uint64_t)deadline->valuedouble ||
            old->supplied_rev != (uint32_t)rev->valuedouble || old->domain != domain || old->target != target || old->minute != minute ||
            old->correlation_id != correlation_id || strcmp(old->client_session, client_session) ||
            old->programs != edit_programs || (edit_programs && (memcmp(old->program_base, base, sizeof(base)) || memcmp(old->program_target, targets, sizeof(targets)))))
            reject(reply, id, true, "id_conflict", NULL);
        else cJSON_AddItemToObject(reply, "command", receipt(old));
        return;
    }
    if (!text_is(get(r, "boot"), boot_id)) error = "boot_changed";
    else if (deadline->valuedouble <= (double)now || deadline->valuedouble - (double)now > 5000) error = "expired";
    else if (!test_enabled() && !gw_vehicle_control_available()) error = "unsupported_operation";
    else if (edit_programs && test_enabled()) error = "unsupported_operation";
    else if (domain != 2 && (uint32_t)rev->valuedouble != generations[domain]) error = "conflict";
    // These refusals remain true for unchanged bytes throughout this boot.
    if (error && (!strcmp(error, "boot_changed") || !strcmp(error, "expired"))) {
        reject(reply, id, true, error, NULL); return;
    }
    const char *reason = !error && test_enabled() && domain != 2 ? prerequisite(domain) : NULL;
    if (reason) error = "precondition_failed";
    command_t *slot = NULL, *active = NULL;
    for (size_t i = 0; i < GW_UDP_COMMAND_CAPACITY; ++i) {
        command_t *c = &commands[i];
        if (c->phase == EMPTY && !slot) slot = c;
        if (c->phase == ACCEPTED && (c->domain == domain || !test_enabled())) active = c;
    }
    if (!error && (!slot || now < overload_until || (active && (!test_enabled() || domain || target)) || generations[domain] == INT32_MAX)) error = "busy";
    if (!slot) {
        // No room to retain this refusal. Prevent a queued duplicate becoming
        // executable when an old record expires, until its deadline has passed.
        if ((uint64_t)deadline->valuedouble > overload_until) overload_until = (uint64_t)deadline->valuedouble;
        reject(reply, id, true, error ? error : "busy", reason); return;
    }
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->client, client, 33); memcpy(slot->id, id, 33);
    slot->correlation_id = correlation_id;
    snprintf(slot->client_session, sizeof(slot->client_session), "%s", client_session);
    slot->created = now; slot->deadline = (uint64_t)deadline->valuedouble;
    slot->supplied_rev = (uint32_t)rev->valuedouble;
    slot->revision = 1; slot->target = target; slot->minute = minute; slot->domain = domain; slot->phase = ACCEPTED;
    slot->programs = edit_programs;
    memcpy(slot->program_base, base, sizeof(base)); memcpy(slot->program_target, targets, sizeof(targets));
    if (error) {
        // Remember refusals too: a delayed retry must not become an action
        // after Busy/other prerequisites have changed underneath the caller.
        slot->phase = REJECTED; slot->error = error; slot->reason = reason; slot->terminal_at = now;
        cJSON_AddItemToObject(reply, "command", receipt(slot)); return;
    }
    if (active) finish(active, FAILED, "superseded", now);
    slot->domain_rev = ++generations[domain];
    if (edit_programs) {
        memcpy(slot->program_base, base, sizeof(base)); memcpy(slot->program_target, targets, sizeof(targets));
        for (unsigned i = 0; i < 4; ++i) {
            varstore_write(GW_VAR_REQUEST_PRECOND_BASE1 + i, base[i]);
            varstore_write(GW_VAR_REQUEST_PRECOND_TARGET1 + i, targets[i]);
        }
    } else if (!domain) {
        varstore_write(GW_VAR_REQUEST_CLIMATE_ENABLED, target);
        varstore_write(GW_VAR_REQUEST_CLIMATE_GENERATION, (int32_t)generations[domain]);
    } else if (domain == 1) {
        varstore_write(GW_VAR_REQUEST_CHARGE_MODE, target);
        if (minute >= 0) varstore_write(GW_VAR_REQUEST_CHARGE_START_MINUTE, minute);
        else varstore_invalidate(GW_VAR_REQUEST_CHARGE_START_MINUTE);
        varstore_write(GW_VAR_REQUEST_CHARGE_GENERATION, (int32_t)generations[domain]);
    }
    if (!test_enabled()) {
        control_requests = control_requests == INT32_MAX ? 1 : control_requests + 1;
        slot->physical = true; slot->control_generation = control_requests;
        varstore_write(GW_VAR_REQUEST_VEHICLE_KIND, edit_programs ? GW_CONTROL_PRECOND_PROGRAMS : domain == 2 ? GW_CONTROL_REFRESH :
            domain == 0 ? (target ? GW_CONTROL_PRECOND_START : GW_CONTROL_PRECOND_STOP) :
            target ? GW_CONTROL_DEFERRED : GW_CONTROL_IMMEDIATE);
        varstore_write(GW_VAR_REQUEST_VEHICLE_MINUTE, minute);
        varstore_write(GW_VAR_REQUEST_VEHICLE_CORRELATION, (int32_t)correlation_id);
        for (unsigned i = 0; i < 4; ++i)
            varstore_write(GW_VAR_REQUEST_VEHICLE_SESSION0 + i, correlation_id ? (int32_t)session_word(client_session, i) : 0);
        varstore_write(GW_VAR_REQUEST_VEHICLE_GENERATION, control_requests);
    }
    if (accepted_count < INT32_MAX) ++accepted_count;
    varstore_write(GW_VAR_UDP_COMMAND_COUNT, (int32_t)accepted_count);
    ESP_LOGI("udp", "corr=%s/%lu accepted %s, message=%s", client_session,
        (unsigned long)correlation_id, get(r, "op")->valuestring, id);
    cJSON_AddItemToObject(reply, "command", receipt(slot));
}
size_t gw_udp_protocol_handle(const char *data, size_t length, uint64_t now, char output[GW_UDP_MAX_BYTES + 1])
{
    if (!data || !length || length > GW_UDP_MAX_BYTES || !input_valid(data, length)) return 0;
    char input[GW_UDP_MAX_BYTES + 1]; memcpy(input, data, length); input[length] = 0;
    cJSON *r = cJSON_ParseWithLengthOpts(input, length + 1, NULL, true);
    if (!cJSON_IsObject(r) || !tree_valid(r, 0) || !id_valid(get(r, "id")) || !id_valid(get(r, "client"))) { cJSON_Delete(r); return 0; }
    gw_udp_protocol_tick(now);
    const char *id = get(r, "id")->valuestring, *client = get(r, "client")->valuestring;
    const cJSON *op = get(r, "op"), *args = get(r, "args");
    const bool climate = text_is(op, "climate.set") || text_is(op, "climate.programs.set"), charge = text_is(op, "charge.mode.set"), refresh = text_is(op, "vehicle.refresh"), write = climate || charge || refresh;
    cJSON *reply = cJSON_CreateObject();
    if (!reply) { cJSON_Delete(r); return 0; }
    cJSON_AddNumberToObject(reply, "v", 0); cJSON_AddStringToObject(reply, "type", "status");
    cJSON_AddStringToObject(reply, "gw", gateway_id); cJSON_AddStringToObject(reply, "boot", boot_id);
    cJSON_AddStringToObject(reply, "reply_to", id); cJSON_AddNumberToObject(reply, "seq", (double)++sequence);
    cJSON_AddNumberToObject(reply, "now_ms", (double)now);
    const char *error = NULL;
    const cJSON *correlation = get(r, "correlation_id"), *session = get(r, "client_session");
    bool valid_correlation = number(correlation, 1, UINT32_MAX) && id_valid(session);
    if (valid_correlation) cJSON_AddNumberToObject(reply, "correlation_id", correlation->valuedouble);
    const cJSON *gw = get(r, "gw");
    if (!number(get(r, "v"), 0, 0)) error = "unsupported_version";
    else if ((correlation || session) && !valid_correlation) error = "invalid_argument";
    else if (!text_is(get(r, "type"), "request") || !cJSON_IsString(op) || !cJSON_IsObject(args)) error = "invalid_argument";
    else if (!(text_is(gw, gateway_id) || (!write && text_is(op, "info.get") && (cJSON_IsNull(gw) || !gw)))) error = "wrong_gateway";
    if (error) reject(reply, id, write, error, NULL);
    else if (write) write_request(r, reply, id, client, refresh ? 2 : charge ? 1 : 0, now);
    else if (text_is(op, "info.get") && !args->child) cJSON_AddItemToObject(reply, "info", info());
    else if (text_is(op, "state.get") && !args->child) cJSON_AddItemToObject(reply, "state", state());
    else if (text_is(op, "vehicle.details.get") && !args->child) cJSON_AddItemToObject(reply, "details", details());
    else if (text_is(op, "climate.programs.get") && !args->child) cJSON_AddItemToObject(reply, "programs", programs());
    else if (text_is(op, "low_power.get") && !args->child)
        cJSON_AddItemToObject(reply, "low_power", gw_low_power_json());
    else if (text_is(op, "low_power.set")) {
        const char *reason = NULL;
        if (!text_is(get(r, "boot"), boot_id)) reject(reply, id, false, "wrong_boot", NULL);
        else if (gw_low_power_save(args, &reason) != ESP_OK) reject(reply, id, false, reason, NULL);
        else cJSON_AddItemToObject(reply, "low_power", gw_low_power_json());
    }
    else if (text_is(op, "auto_refresh.get") && !args->child)
        cJSON_AddItemToObject(reply, "auto_refresh", gw_auto_refresh_json());
    else if (text_is(op, "auto_refresh.set")) {
        const char *reason = NULL;
        if (!text_is(get(r, "boot"), boot_id)) reject(reply, id, false, "wrong_boot", NULL);
        else if (gw_auto_refresh_save(args, &reason) != ESP_OK) reject(reply, id, false, reason, NULL);
        else cJSON_AddItemToObject(reply, "auto_refresh", gw_auto_refresh_json());
    }
    else if (text_is(op, "command.get")) {
        static const char *const query_keys[] = {"command_id"};
        bool by_correlation = !args->child && valid_correlation;
        if (!by_correlation && (!keys(args, query_keys, 1) || !id_valid(get(args, "command_id")))) reject(reply, id, false, "invalid_argument", NULL);
        else {
            command_t *c = by_correlation ? find_correlation(client, session->valuestring, (uint32_t)correlation->valuedouble) :
                find(client, get(args, "command_id")->valuestring);
            if (c && valid_correlation && (c->correlation_id != (uint32_t)correlation->valuedouble || strcmp(c->client_session, session->valuestring)))
                reject(reply, id, false, "id_conflict", NULL);
            else if (c) cJSON_AddItemToObject(reply, "command", receipt(c));
            else reject(reply, id, false, "result_unknown", NULL);
        }
    } else reject(reply, id, false, "unsupported_operation", NULL);
    if (valid_correlation) {
        cJSON *command_reply = cJSON_GetObjectItemCaseSensitive(reply, "command");
        if (command_reply && !get(command_reply, "correlation_id")) {
            cJSON_AddNumberToObject(command_reply, "correlation_id", correlation->valuedouble);
            cJSON_AddStringToObject(command_reply, "client_session", session->valuestring);
        }
    }
    // Keep core state within one datagram. Optional electrical/battery readings
    // can be omitted when large ages/numbers exhaust the v0 envelope budget.
    static const char *const optional[] = {"current_a", "voltage_v", "battery_max_c", "battery_min_c",
        "evaporator_c", "climate_left_setpoint_c", "climate_right_setpoint_c"};
    bool fits = cJSON_PrintPreallocated(reply, output, GW_UDP_MAX_BYTES + 1, false);
    for (size_t i = 0; !fits && i < sizeof(optional) / sizeof(optional[0]); ++i) {
        cJSON *s = cJSON_GetObjectItemCaseSensitive(reply, "state");
        if (s) cJSON_DeleteItemFromObjectCaseSensitive(s, optional[i]);
        else {
            cJSON *fields = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(reply, "info"), "fields");
            if (!fields) break;
            for (int n = cJSON_GetArraySize(fields) - 1; n >= 0; --n)
                if (text_is(cJSON_GetArrayItem(fields, n), optional[i])) cJSON_DeleteItemFromArray(fields, n);
        }
        fits = cJSON_PrintPreallocated(reply, output, GW_UDP_MAX_BYTES + 1, false);
    }
    cJSON_Delete(reply); cJSON_Delete(r);
    return fits ? strlen(output) : 0;
}
