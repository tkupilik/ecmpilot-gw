// SPDX-License-Identifier: BSD-2-Clause
#include "udp_protocol.h"
#include "process_data.h"
#include "cJSON.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RETENTION_MS 600000
#define CONFIRM_MS 30000
#define MAX_SAFE_INTEGER 9007199254740991.0
typedef enum { EMPTY, ACCEPTED, SUCCEEDED, FAILED, UNKNOWN } phase_t;
typedef struct {
    char client[33], id[33];
    uint64_t created, terminal_at, deadline;
    uint32_t supplied_rev, domain_rev, revision;
    int target, minute;
    unsigned domain;
    phase_t phase;
    const char *error;
} command_t;
static command_t commands[GW_UDP_COMMAND_CAPACITY];
static char gateway_id[81], boot_id[33], firmware_version[81];
static uint64_t sequence;
static uint32_t generations[2], accepted_count;

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
        if (!test_enabled()) { finish(c, FAILED, "test_mode_disabled", now); continue; }
        if (now - c->created >= CONFIRM_MS) { finish(c, UNKNOWN, "unconfirmed", now); continue; }
        // Do not accept an observation older than this command as readback.
        // Forced values are deliberately current effective observations.
        size_t id = c->domain ? GW_VAR_CHARGE_MODE : GW_VAR_CLIMATE_STATE;
        varstore_snapshot_t s = sample(id);
        bool confirmed = known(s, 0, 1) && s.value == c->target && s.age_ms <= 5000 && s.age_ms <= now - c->created;
        if (confirmed && c->domain && c->target == 1) {
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
    generations[0] = generations[1] = accepted_count = 0; sequence = 0;
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
    static const char *const phases[] = {"unknown", "accepted", "succeeded", "failed", "unknown"};
    return result(c->id, c->revision, c->domain_rev, phases[c->phase], c->error);
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
    {"cabin_c", GW_VAR_CABIN_C_X10, 10, -600, 1000},
    {"outside_c", GW_VAR_OUTSIDE_C_X10, 10, -600, 1000},
    {"battery_min_c", GW_VAR_BATTERY_MIN_C_X10, 10, -600, 1500},
    {"battery_max_c", GW_VAR_BATTERY_MAX_C_X10, 10, -600, 1500},
    {"odometer_km", GW_VAR_ODOMETER_KM, 1, 0, 10000000},
    {"voltage_v", GW_VAR_VOLTAGE_V_X10, 10, 0, 10000},
    {"current_a", GW_VAR_CURRENT_A_X10, 10, -20000, 20000},
};
static cJSON *info(void)
{
    cJSON *j = cJSON_CreateObject(), *fields = cJSON_AddArrayToObject(j, "fields");
    cJSON_AddStringToObject(j, "firmware", firmware_version);
    cJSON_AddStringToObject(j, "security", "lab-plain");
    const bool test = test_enabled();
    cJSON_AddStringToObject(j, "control_backend", test ? "varstore-test" : "none");
    if (test) cJSON_AddStringToObject(j, "timezone", "Europe/Prague"); else cJSON_AddNullToObject(j, "timezone");
    for (size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i)
        cJSON_AddItemToArray(fields, cJSON_CreateString(metrics[i].name));
    cJSON *operations = cJSON_AddObjectToObject(j, "operations");
    if (test) {
        cJSON *climate = cJSON_AddObjectToObject(operations, "climate.set");
        cJSON_AddBoolToObject(climate, "verified", false);
        cJSON_AddBoolToObject(climate, "confirmation", true);
        cJSON *values = cJSON_AddArrayToObject(climate, "enabled");
        cJSON_AddItemToArray(values, cJSON_CreateBool(true)); cJSON_AddItemToArray(values, cJSON_CreateBool(false));
        cJSON *charge = cJSON_AddObjectToObject(operations, "charge.mode.set");
        cJSON_AddBoolToObject(charge, "verified", false); cJSON_AddBoolToObject(charge, "confirmation", true);
        cJSON_AddBoolToObject(charge, "persistent_schedule", false);
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
    if (known(mode, 0, 1) && mode.value) {
        varstore_snapshot_t start = sample(GW_VAR_CHARGE_START_MINUTE);
        if (known(start, 0, 1439)) {
            const unsigned minutes = (unsigned)start.value;
            char time[6] = {(char)('0' + minutes / 600), (char)('0' + minutes / 60 % 10), ':',
                            (char)('0' + minutes % 60 / 10), (char)('0' + minutes % 10), 0};
            cJSON_AddStringToObject(q, "start_local", time); age = older(age, start.age_ms);
        } else { cJSON_AddNullToObject(q, "start_local"); valid = false; }
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
    static const char *const root_keys[] = {"v","type","gw","boot","client","id","deadline_ms","domain_rev","op","args"};
    static const char *const climate_keys[] = {"enabled"};
    static const char *const immediate_keys[] = {"mode"};
    static const char *const deferred_keys[] = {"mode","start_local","timezone"};
    int target = 0, minute = -1;
    const char *error = NULL;
    if (!keys(r, root_keys, 10)) error = "unsupported_argument";
    else if (!domain) {
        if (!keys(args, climate_keys, 1) || !cJSON_IsBool(get(args, "enabled"))) error = "unsupported_argument";
        else target = cJSON_IsTrue(get(args, "enabled"));
    } else if (text_is(get(args, "mode"), "immediate")) {
        if (!keys(args, immediate_keys, 1)) error = "unsupported_argument";
    } else if (text_is(get(args, "mode"), "deferred")) {
        target = 1;
        if (!keys(args, deferred_keys, 3)) error = "unsupported_argument";
        else if (!parse_time(get(args, "start_local"), &minute) || !text_is(get(args, "timezone"), "Europe/Prague")) error = "invalid_argument";
    } else error = "invalid_argument";
    if (!error && (!number(deadline, 0, MAX_SAFE_INTEGER) || !number(rev, 0, INT32_MAX))) error = "invalid_argument";
    if (error) { reject(reply, id, true, error, NULL); return; }
    command_t *old = find(client, id);
    if (old) {
        if (!text_is(get(r, "boot"), boot_id) || old->deadline != (uint64_t)deadline->valuedouble ||
            old->supplied_rev != (uint32_t)rev->valuedouble || old->domain != domain || old->target != target || old->minute != minute)
            reject(reply, id, true, "id_conflict", NULL);
        else cJSON_AddItemToObject(reply, "command", receipt(old));
        return;
    }
    if (!text_is(get(r, "boot"), boot_id)) error = "boot_changed";
    else if (deadline->valuedouble <= (double)now || deadline->valuedouble - (double)now > 5000) error = "expired";
    else if (!test_enabled()) error = "unsupported_operation";
    else if ((uint32_t)rev->valuedouble != generations[domain]) error = "conflict";
    if (error) { reject(reply, id, true, error, NULL); return; }
    const char *reason = prerequisite(domain);
    if (reason) { reject(reply, id, true, "precondition_failed", reason); return; }
    command_t *slot = NULL, *active = NULL;
    for (size_t i = 0; i < GW_UDP_COMMAND_CAPACITY; ++i) {
        command_t *c = &commands[i];
        if (c->phase == EMPTY && !slot) slot = c;
        if (c->phase == ACCEPTED && c->domain == domain) active = c;
    }
    if (!slot || (active && (domain || target)) || generations[domain] == INT32_MAX) {
        reject(reply, id, true, "busy", NULL); return;
    }
    if (active) finish(active, FAILED, "superseded", now);
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->client, client, 33); memcpy(slot->id, id, 33);
    slot->created = now; slot->deadline = (uint64_t)deadline->valuedouble;
    slot->supplied_rev = (uint32_t)rev->valuedouble; slot->domain_rev = ++generations[domain];
    slot->revision = 1; slot->target = target; slot->minute = minute; slot->domain = domain; slot->phase = ACCEPTED;
    if (!domain) {
        varstore_write(GW_VAR_REQUEST_CLIMATE_ENABLED, target);
        varstore_write(GW_VAR_REQUEST_CLIMATE_GENERATION, (int32_t)generations[domain]);
    } else {
        varstore_write(GW_VAR_REQUEST_CHARGE_MODE, target);
        if (target) varstore_write(GW_VAR_REQUEST_CHARGE_START_MINUTE, minute);
        else varstore_invalidate(GW_VAR_REQUEST_CHARGE_START_MINUTE);
        varstore_write(GW_VAR_REQUEST_CHARGE_GENERATION, (int32_t)generations[domain]);
    }
    if (accepted_count < INT32_MAX) ++accepted_count;
    varstore_write(GW_VAR_UDP_COMMAND_COUNT, (int32_t)accepted_count);
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
    const bool climate = text_is(op, "climate.set"), charge = text_is(op, "charge.mode.set"), write = climate || charge;
    cJSON *reply = cJSON_CreateObject();
    if (!reply) { cJSON_Delete(r); return 0; }
    cJSON_AddNumberToObject(reply, "v", 0); cJSON_AddStringToObject(reply, "type", "status");
    cJSON_AddStringToObject(reply, "gw", gateway_id); cJSON_AddStringToObject(reply, "boot", boot_id);
    cJSON_AddStringToObject(reply, "reply_to", id); cJSON_AddNumberToObject(reply, "seq", (double)++sequence);
    cJSON_AddNumberToObject(reply, "now_ms", (double)now);
    const char *error = NULL;
    const cJSON *gw = get(r, "gw");
    if (!number(get(r, "v"), 0, 0)) error = "unsupported_version";
    else if (!text_is(get(r, "type"), "request") || !cJSON_IsString(op) || !cJSON_IsObject(args)) error = "invalid_argument";
    else if (!(text_is(gw, gateway_id) || (!write && text_is(op, "info.get") && (cJSON_IsNull(gw) || !gw)))) error = "wrong_gateway";
    if (error) reject(reply, id, write, error, NULL);
    else if (write) write_request(r, reply, id, client, charge ? 1 : 0, now);
    else if (text_is(op, "info.get") && !args->child) cJSON_AddItemToObject(reply, "info", info());
    else if (text_is(op, "state.get") && !args->child) cJSON_AddItemToObject(reply, "state", state());
    else if (text_is(op, "command.get")) {
        static const char *const query_keys[] = {"command_id"};
        if (!keys(args, query_keys, 1) || !id_valid(get(args, "command_id"))) reject(reply, id, false, "invalid_argument", NULL);
        else {
            command_t *c = find(client, get(args, "command_id")->valuestring);
            if (c) cJSON_AddItemToObject(reply, "command", receipt(c));
            else reject(reply, id, false, "result_unknown", NULL);
        }
    } else reject(reply, id, false, "unsupported_operation", NULL);
    // Keep core state within one datagram. Optional electrical/battery readings
    // can be omitted when large ages/numbers exhaust the v0 envelope budget.
    static const char *const optional[] = {"current_a", "voltage_v", "battery_max_c", "battery_min_c"};
    bool fits = cJSON_PrintPreallocated(reply, output, GW_UDP_MAX_BYTES + 1, false);
    for (size_t i = 0; !fits && i < sizeof(optional) / sizeof(optional[0]); ++i) {
        cJSON *s = cJSON_GetObjectItemCaseSensitive(reply, "state");
        if (!s) break;
        cJSON_DeleteItemFromObjectCaseSensitive(s, optional[i]);
        fits = cJSON_PrintPreallocated(reply, output, GW_UDP_MAX_BYTES + 1, false);
    }
    cJSON_Delete(reply); cJSON_Delete(r);
    return fits ? strlen(output) : 0;
}
