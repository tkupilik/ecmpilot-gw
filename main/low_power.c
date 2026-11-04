// SPDX-License-Identifier: BSD-2-Clause
#include "low_power.h"
#include "power_policy.h"
#include "process_data.h"
#include "vehicle_control.h"
#include "can_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "pt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct { uint32_t version, revision, enabled, idle_hours; } config_t;
static config_t saved;
static SemaphoreHandle_t config_lock;
static struct {
    struct pt pt;
    uint64_t previous_ms;
    uint32_t retry_tmr;
    bool inflight;
} c;

/*****************************************************************************/

static int32_t raw(size_t id, int32_t fallback)
{
    varstore_snapshot_t sample = varstore_inspect(id, 0);
    return sample.raw_valid ? sample.raw_value : fallback;
}

static bool valid(const config_t *s)
{
    return s->version == 1 && s->revision > 0 && s->revision <= INT32_MAX &&
        s->enabled <= 1 && s->idle_hours >= 1 && s->idle_hours <= GW_LOW_POWER_MAX_HOURS;
}

static bool config(config_t *s)
{
    int32_t revision = raw(GW_VAR_LOW_POWER_REVISION, 0);
    *s = (config_t){1, revision, varstore_read(GW_VAR_LOW_POWER_ENABLED, 0, -1),
        varstore_read(GW_VAR_LOW_POWER_IDLE_HOURS, 0, -1)};
    return valid(s) && revision == raw(GW_VAR_LOW_POWER_REVISION, 0);
}

static void publish(void)
{
    varstore_invalidate(GW_VAR_LOW_POWER_REVISION);
    varstore_write(GW_VAR_LOW_POWER_ENABLED, saved.enabled);
    varstore_write(GW_VAR_LOW_POWER_IDLE_HOURS, saved.idle_hours);
    varstore_write(GW_VAR_LOW_POWER_REVISION, saved.revision);
}

static bool observation(varstore_snapshot_t *sample)
{
    // Zero max age checks the validity flag without expiring an old CAN sample.
    // A Force cannot create activity, hide real activity or arm a fresh boot.
    *sample = varstore_inspect(GW_VAR_BSI_POWER_MANAGEMENT, 0);
    return sample->raw_valid && sample->raw_value >= 0 && sample->raw_value <= 4;
}

static bool capable(void) { return GW_CAN_ENABLED && GW_CAN_IS_INFO; }

bool gw_low_power_eligible(void)
{
    config_t s;
    varstore_snapshot_t sample;
    return gw_low_power_permitted() && config(&s) && s.enabled && observation(&sample) &&
        (uint64_t)sample.raw_age_ms >= (uint64_t)s.idle_hours * 3600000ULL;
}

bool gw_low_power_permitted(void)
{
    config_t s;
    return capable() && config(&s) && s.enabled && !gw_vehicle_control_busy() && !raw(GW_VAR_TEST_ENABLED, 0) &&
        !raw(GW_VAR_OTA_ACTIVE, 0);
}

/*****************************************************************************/

esp_err_t gw_low_power_init(void)
{
    if (!config_lock) config_lock = xSemaphoreCreateMutex();
    if (!config_lock) return ESP_ERR_NO_MEM;
    saved = (config_t){1, 1, 1, 48};
    nvs_handle_t handle;
    esp_err_t error = nvs_open("gateway", NVS_READONLY, &handle);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return error;
    if (error == ESP_OK) {
        config_t candidate;
        size_t size = sizeof(candidate);
        error = nvs_get_blob(handle, "low_power", &candidate, &size);
        nvs_close(handle);
        if (error == ESP_OK) {
            if (size != sizeof(candidate) || !valid(&candidate)) return ESP_ERR_INVALID_STATE;
            saved = candidate;
        } else if (error != ESP_ERR_NVS_NOT_FOUND) return error;
    }
    memset(&c, 0, sizeof(c));
    PT_INIT(&c.pt);
    c.previous_ms = (uint64_t)esp_timer_get_time() / 1000;
    publish();
    varstore_write(GW_VAR_LOW_POWER_STATE, GW_LOW_POWER_WAIT_DATA);
    varstore_invalidate(GW_VAR_LOW_POWER_REMAINING_S);
    varstore_write(GW_VAR_LOW_POWER_REQUEST_COUNT, 0);
    varstore_write(GW_VAR_LOW_POWER_LAST_ERROR, ESP_OK);
    return ESP_OK;
}

/*****************************************************************************/

static PT_THREAD(AutomaticSleep(void))
{
    PT_BEGIN(&c.pt);
    (void)PT_YIELD_FLAG;
    while (true) {
        config_t s;
        varstore_snapshot_t sample;
        int state;
        varstore_invalidate(GW_VAR_LOW_POWER_REMAINING_S);
        if (c.inflight && raw(GW_VAR_REQUEST_PWR_GENERATION, 0) == raw(GW_VAR_PWR_COMPLETED, 0)) {
            c.inflight = false;
            c.retry_tmr = 60000;
            varstore_write(GW_VAR_LOW_POWER_LAST_ERROR, raw(GW_VAR_PWR_LAST_ERROR, ESP_FAIL));
        }
        if (c.inflight) state = GW_LOW_POWER_ENTERING;
        else if (!config(&s)) state = GW_LOW_POWER_INVALID_CONFIG;
        else if (!s.enabled) state = GW_LOW_POWER_DISABLED;
        else if (!capable()) state = GW_LOW_POWER_UNAVAILABLE;
        else if (!observation(&sample)) state = GW_LOW_POWER_WAIT_DATA;
        else {
            uint64_t timeout = (uint64_t)s.idle_hours * 3600000ULL;
            uint64_t remaining = sample.raw_age_ms < timeout ? timeout - sample.raw_age_ms : 0;
            // Seconds remain far below INT32_MAX even at the 30-day maximum.
            varstore_write(GW_VAR_LOW_POWER_REMAINING_S, (int32_t)((remaining + 999) / 1000));
            if (remaining) state = GW_LOW_POWER_COUNTING;
            else if (c.retry_tmr) state = GW_LOW_POWER_RETRY;
            else if (!gw_low_power_eligible() ||
                     raw(GW_VAR_REQUEST_PWR_GENERATION, 0) != raw(GW_VAR_PWR_COMPLETED, 0)) state = GW_LOW_POWER_BUSY;
            else {
                esp_err_t error = gw_power_auto_request();
                varstore_write(GW_VAR_LOW_POWER_LAST_ERROR, error);
                if (error == ESP_OK) {
                    int32_t count = raw(GW_VAR_LOW_POWER_REQUEST_COUNT, 0);
                    if (count < INT32_MAX) varstore_write(GW_VAR_LOW_POWER_REQUEST_COUNT, count + 1);
                    c.inflight = true;
                    ESP_LOGI("low_power", "Automatic CAN-wake deep sleep requested: BSI age=%lu s, timeout=%lu h",
                        (unsigned long)(sample.raw_age_ms / 1000), (unsigned long)s.idle_hours);
                    state = GW_LOW_POWER_ENTERING;
                } else { c.retry_tmr = 60000; state = GW_LOW_POWER_RETRY; }
            }
        }
        varstore_write(GW_VAR_LOW_POWER_STATE, state);
        PT_YIELD(&c.pt);
    }
    PT_END(&c.pt);
}

void gw_low_power_tick(void)
{
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
    uint64_t elapsed = now >= c.previous_ms ? now - c.previous_ms : 0;
    c.previous_ms = now;
    c.retry_tmr = c.retry_tmr > elapsed ? c.retry_tmr - (uint32_t)elapsed : 0;
    (void)PT_SCHEDULE(AutomaticSleep());
}

/*****************************************************************************/

cJSON *gw_low_power_json(void)
{
    config_t s;
    bool ok = config(&s);
    if (!ok) s = saved;
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddNumberToObject(j, "rev", s.revision);
    cJSON_AddBoolToObject(j, "enabled", s.enabled != 0);
    cJSON_AddNumberToObject(j, "idle_hours", s.idle_hours);
    cJSON_AddBoolToObject(j, "config_valid", ok);
    cJSON_AddBoolToObject(j, "capable", capable());
    static const char *const states[] = {"disabled", "waiting_data", "counting", "busy", "entering", "retry", "unavailable", "invalid_config"};
    int state = raw(GW_VAR_LOW_POWER_STATE, GW_LOW_POWER_WAIT_DATA);
    cJSON_AddStringToObject(j, "state", state >= 0 && state <= GW_LOW_POWER_INVALID_CONFIG ? states[state] : "invalid_config");
    varstore_snapshot_t remaining = varstore_inspect(GW_VAR_LOW_POWER_REMAINING_S, 0);
    if (remaining.raw_valid) cJSON_AddNumberToObject(j, "remaining_s", remaining.raw_value);
    else cJSON_AddNullToObject(j, "remaining_s");
    cJSON_AddNumberToObject(j, "request_count", raw(GW_VAR_LOW_POWER_REQUEST_COUNT, 0));
    cJSON_AddNumberToObject(j, "last_error", raw(GW_VAR_LOW_POWER_LAST_ERROR, 0));
    return j;
}

esp_err_t gw_low_power_save(const cJSON *args, const char **reason)
{
    *reason = "invalid_argument";
    if (!cJSON_IsObject(args)) return ESP_ERR_INVALID_ARG;
    static const char *const names[] = {"rev", "enabled", "idle_hours"};
    const cJSON *items[3] = {0};
    for (const cJSON *p = args->child; p; p = p->next) {
        unsigned i;
        for (i = 0; i < 3; ++i) if (!strcmp(p->string, names[i])) break;
        if (i == 3 || items[i]) return ESP_ERR_INVALID_ARG;
        items[i] = p;
    }
    if (!cJSON_IsNumber(items[0]) || !isfinite(items[0]->valuedouble) ||
        floor(items[0]->valuedouble) != items[0]->valuedouble || items[0]->valuedouble < 1 || items[0]->valuedouble > INT32_MAX ||
        !cJSON_IsBool(items[1]) || !cJSON_IsNumber(items[2]) || !isfinite(items[2]->valuedouble) ||
        floor(items[2]->valuedouble) != items[2]->valuedouble || items[2]->valuedouble < 1 || items[2]->valuedouble > GW_LOW_POWER_MAX_HOURS)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(config_lock, portMAX_DELAY);
    config_t next = {1, 1, cJSON_IsTrue(items[1]), items[2]->valueint};
    if (next.enabled == saved.enabled && next.idle_hours == saved.idle_hours) {
        xSemaphoreGive(config_lock); *reason = NULL; return ESP_OK;
    }
    if ((uint32_t)items[0]->valueint != saved.revision) {
        xSemaphoreGive(config_lock); *reason = "settings_conflict"; return ESP_ERR_INVALID_STATE;
    }
    next.revision = saved.revision == INT32_MAX ? 1 : saved.revision + 1;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("gateway", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "low_power", &next, sizeof(next));
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    if (error == ESP_OK) { saved = next; publish(); }
    xSemaphoreGive(config_lock);
    *reason = error == ESP_OK ? NULL : "storage_error";
    return error;
}
