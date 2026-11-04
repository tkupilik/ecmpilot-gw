// SPDX-License-Identifier: BSD-2-Clause
#include "auto_refresh.h"
#include "process_data.h"
#include "vehicle_control.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static SemaphoreHandle_t config_lock;
static gw_auto_refresh_config_t saved;

static bool valid(const gw_auto_refresh_config_t *s)
{
    return s->version == 2 && s->revision > 0 && s->revision <= INT32_MAX && s->enabled <= 1 &&
        s->standby_min >= 1 && s->standby_min <= GW_AUTO_MAX_PERIOD_MIN &&
        s->charging_min >= 1 && s->charging_min <= GW_AUTO_MAX_PERIOD_MIN &&
        s->min_aux_soc_pct_x10 <= 1000 && s->min_aux_soc_pct_x10 % 10 == 0 &&
        s->charge_limit_enabled <= 1 && (!s->charge_limit_enabled || s->enabled) &&
        s->charge_limit_soc_pct_x10 >= 10 && s->charge_limit_soc_pct_x10 <= 1000 &&
        s->charge_limit_soc_pct_x10 % 10 == 0;
}

static void publish(void)
{
    // A reader must see the same valid revision before and after reading fields.
    varstore_invalidate(GW_VAR_AUTO_REFRESH_REVISION);
    varstore_write(GW_VAR_AUTO_REFRESH_ENABLED, saved.enabled);
    varstore_write(GW_VAR_AUTO_REFRESH_STANDBY_MIN, saved.standby_min);
    varstore_write(GW_VAR_AUTO_REFRESH_CHARGING_MIN, saved.charging_min);
    varstore_write(GW_VAR_AUTO_REFRESH_MIN_AUX_SOC_PCT_X10, saved.min_aux_soc_pct_x10);
    varstore_write(GW_VAR_CHARGE_LIMIT_ENABLED, saved.charge_limit_enabled);
    varstore_write(GW_VAR_CHARGE_LIMIT_SOC_PCT_X10, saved.charge_limit_soc_pct_x10);
    varstore_write(GW_VAR_AUTO_REFRESH_REVISION, saved.revision);
}

esp_err_t gw_auto_refresh_init(void)
{
    if (!config_lock) config_lock = xSemaphoreCreateMutex();
    if (!config_lock) return ESP_ERR_NO_MEM;
    saved = (gw_auto_refresh_config_t){2, 1, 0, 60, 5, 500, 0, 800};
    nvs_handle_t handle;
    esp_err_t err = nvs_open("gateway", NVS_READONLY, &handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (err == ESP_OK) {
        gw_auto_refresh_config_t candidate = saved;
        size_t size = sizeof(candidate);
        err = nvs_get_blob(handle, "auto_refresh", &candidate, &size);
        nvs_close(handle);
        if (err == ESP_OK) {
            // Preserve deployed v1 refresh settings; the new limiter starts off.
            if (size == 6 * sizeof(uint32_t) && candidate.version == 1) {
                candidate.version = 2; size = sizeof(candidate);
            }
            if (size != sizeof(candidate) || !valid(&candidate)) return ESP_ERR_INVALID_STATE;
            saved = candidate;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    }
    publish();
    return ESP_OK;
}

bool gw_auto_refresh_config(gw_auto_refresh_config_t *out)
{
    varstore_snapshot_t revision = varstore_inspect(GW_VAR_AUTO_REFRESH_REVISION, 0);
    if (!revision.raw_valid || revision.raw_value <= 0) return false;
    gw_auto_refresh_config_t s = {2, (uint32_t)revision.raw_value,
        (uint32_t)varstore_read(GW_VAR_AUTO_REFRESH_ENABLED, 0, -1),
        (uint32_t)varstore_read(GW_VAR_AUTO_REFRESH_STANDBY_MIN, 0, -1),
        (uint32_t)varstore_read(GW_VAR_AUTO_REFRESH_CHARGING_MIN, 0, -1),
        (uint32_t)varstore_read(GW_VAR_AUTO_REFRESH_MIN_AUX_SOC_PCT_X10, 0, -1),
        (uint32_t)varstore_read(GW_VAR_CHARGE_LIMIT_ENABLED, 0, -1),
        (uint32_t)varstore_read(GW_VAR_CHARGE_LIMIT_SOC_PCT_X10, 0, -1)};
    varstore_snapshot_t after = varstore_inspect(GW_VAR_AUTO_REFRESH_REVISION, 0);
    if (!after.raw_valid || after.raw_value != revision.raw_value || !valid(&s)) return false;
    *out = s;
    return true;
}

cJSON *gw_auto_refresh_json(void)
{
    gw_auto_refresh_config_t s;
    bool config_valid = gw_auto_refresh_config(&s);
    if (!config_valid) {
        xSemaphoreTake(config_lock, portMAX_DELAY);
        s = saved;
        xSemaphoreGive(config_lock);
    }
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddNumberToObject(j, "rev", s.revision);
    cJSON_AddBoolToObject(j, "enabled", s.enabled != 0);
    cJSON_AddNumberToObject(j, "standby_min", s.standby_min);
    cJSON_AddNumberToObject(j, "charging_min", s.charging_min);
    cJSON_AddNumberToObject(j, "min_aux_soc_pct", s.min_aux_soc_pct_x10 / 10.0);
    cJSON_AddBoolToObject(j, "charge_limit_enabled", s.charge_limit_enabled != 0);
    cJSON_AddNumberToObject(j, "charge_limit_soc_pct", s.charge_limit_soc_pct_x10 / 10);
    static const char *const limit_states[] = {"disabled", "monitoring", "waiting_data", "busy", "requesting", "limit_reached", "retry", "unavailable", "low_aux_battery"};
    int limit = varstore_read(GW_VAR_CHARGE_LIMIT_STATE, 0, GW_LIMIT_DISABLED);
    cJSON_AddStringToObject(j, "charge_limit_state", limit >= 0 && limit <= GW_LIMIT_LOW_BATTERY ? limit_states[limit] : "unavailable");
    cJSON_AddNumberToObject(j, "charge_limit_last_result", varstore_read(GW_VAR_CHARGE_LIMIT_LAST_RESULT, 0, 0));
    cJSON_AddBoolToObject(j, "config_valid", config_valid);
    cJSON_AddBoolToObject(j, "capable", gw_vehicle_control_available());
    static const char *const states[] = {"disabled", "counting", "low_aux_battery", "unavailable", "busy", "invalid_config", "refreshing"};
    int state = varstore_read(GW_VAR_AUTO_REFRESH_STATE, 0, GW_AUTO_DISABLED);
    cJSON_AddStringToObject(j, "state", state >= 0 && state <= GW_AUTO_EXECUTING ? states[state] : "invalid_config");
    cJSON_AddNumberToObject(j, "remaining_ms", varstore_read(GW_VAR_AUTO_REFRESH_REMAINING_MS, 0, 0));
    cJSON_AddNumberToObject(j, "request_count", varstore_read(GW_VAR_AUTO_REFRESH_REQUEST_COUNT, 0, 0));
    cJSON_AddNumberToObject(j, "last_result", varstore_read(GW_VAR_AUTO_REFRESH_LAST_RESULT, 0, 0));
    return j;
}

static bool integer(const cJSON *value, int lo, int hi)
{
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
        floor(value->valuedouble) == value->valuedouble && value->valuedouble >= lo && value->valuedouble <= hi;
}

esp_err_t gw_auto_refresh_save(const cJSON *args, const char **reason)
{
    *reason = "invalid_argument";
    static const char *const names[] = {"rev", "enabled", "standby_min", "charging_min", "min_aux_soc_pct", "charge_limit_enabled", "charge_limit_soc_pct"};
    const cJSON *items[7] = {0};
    if (!cJSON_IsObject(args)) return ESP_ERR_INVALID_ARG;
    for (const cJSON *p = args->child; p; p = p->next) {
        unsigned i;
        for (i = 0; i < 7; ++i) if (!strcmp(p->string, names[i])) break;
        if (i == 7 || items[i]) return ESP_ERR_INVALID_ARG;
        items[i] = p;
    }
    if (!integer(items[0], 1, INT32_MAX) || !cJSON_IsBool(items[1]) ||
        !integer(items[2], 1, GW_AUTO_MAX_PERIOD_MIN) || !integer(items[3], 1, GW_AUTO_MAX_PERIOD_MIN) ||
        !integer(items[4], 0, 100) || (items[5] && !cJSON_IsBool(items[5])) ||
        (items[6] && !integer(items[6], 1, 100))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(config_lock, portMAX_DELAY);
    gw_auto_refresh_config_t next = {2, 1, cJSON_IsTrue(items[1]),
        (uint32_t)items[2]->valueint, (uint32_t)items[3]->valueint, (uint32_t)items[4]->valueint * 10,
        items[5] ? cJSON_IsTrue(items[5]) : saved.charge_limit_enabled,
        items[6] ? (uint32_t)items[6]->valueint * 10 : saved.charge_limit_soc_pct_x10};
    if (!next.enabled) next.charge_limit_enabled = 0;
    if (!valid(&next)) { xSemaphoreGive(config_lock); return ESP_ERR_INVALID_ARG; }
    if (next.enabled == saved.enabled && next.standby_min == saved.standby_min &&
        next.charging_min == saved.charging_min && next.min_aux_soc_pct_x10 == saved.min_aux_soc_pct_x10 &&
        next.charge_limit_enabled == saved.charge_limit_enabled && next.charge_limit_soc_pct_x10 == saved.charge_limit_soc_pct_x10) {
        xSemaphoreGive(config_lock); *reason = NULL; return ESP_OK;
    }
    if ((uint32_t)items[0]->valueint != saved.revision) {
        xSemaphoreGive(config_lock); *reason = "settings_conflict"; return ESP_ERR_INVALID_STATE;
    }
    next.revision = saved.revision == INT32_MAX ? 1 : saved.revision + 1;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("gateway", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "auto_refresh", &next, sizeof(next));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err == ESP_OK) { saved = next; publish(); }
    xSemaphoreGive(config_lock);
    *reason = err == ESP_OK ? NULL : "storage_error";
    return err;
}
