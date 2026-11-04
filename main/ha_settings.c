// SPDX-License-Identifier: BSD-2-Clause
#include "ha_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static SemaphoreHandle_t lock;
static gw_ha_settings_t saved;

static bool valid(const gw_ha_settings_t *s)
{
    size_t n = strnlen(s->host, sizeof(s->host));
    if (s->version != 1 || !s->revision || s->revision > INT32_MAX || s->enabled > 1 ||
        !s->port || s->port > 65535 || s->period_s < 1 || s->period_s > 60 ||
        n >= sizeof(s->host) || (s->enabled && !n) ||
        strnlen(s->username, sizeof(s->username)) >= sizeof(s->username) ||
        strnlen(s->password, sizeof(s->password)) >= sizeof(s->password)) return false;
    // A hostname or IPv4 address, never a URI with embedded credentials/path.
    for (size_t i = 0; i < n; ++i) {
        char c = s->host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
    }
    return true;
}

esp_err_t gw_ha_settings_init(void)
{
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    saved = (gw_ha_settings_t){.version=1, .revision=1, .port=1883, .period_s=1};
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    gw_ha_settings_t candidate;
    size_t size = sizeof(candidate);
    err = nvs_get_blob(h, "ha_mqtt", &candidate, &size);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(candidate) || !valid(&candidate)) return ESP_ERR_INVALID_STATE;
    saved = candidate;
    return ESP_OK;
}

void gw_ha_settings_get(gw_ha_settings_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = saved;
    xSemaphoreGive(lock);
}

cJSON *gw_ha_settings_json(void)
{
    gw_ha_settings_t s;
    gw_ha_settings_get(&s);
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddNumberToObject(j, "rev", s.revision);
    cJSON_AddBoolToObject(j, "enabled", s.enabled != 0);
    cJSON_AddStringToObject(j, "host", s.host);
    cJSON_AddNumberToObject(j, "port", s.port);
    cJSON_AddStringToObject(j, "username", s.username);
    cJSON_AddBoolToObject(j, "has_password", s.password[0] != 0);
    cJSON_AddNumberToObject(j, "period_s", s.period_s);
    return j;
}

static bool integer(const cJSON *j, int lo, int hi)
{
    return cJSON_IsNumber(j) && isfinite(j->valuedouble) && floor(j->valuedouble) == j->valuedouble &&
        j->valuedouble >= lo && j->valuedouble <= hi;
}

esp_err_t gw_ha_settings_save(const cJSON *args, const char **reason)
{
    *reason = "invalid_argument";
    const char *names[] = {"rev", "enabled", "host", "port", "username", "password", "period_s"};
    const cJSON *items[7] = {0};
    if (!cJSON_IsObject(args)) return ESP_ERR_INVALID_ARG;
    for (const cJSON *p = args->child; p; p = p->next) {
        unsigned i;
        for (i = 0; i < 7; ++i) if (!strcmp(p->string, names[i])) break;
        if (i == 7 || items[i]) return ESP_ERR_INVALID_ARG;
        items[i] = p;
    }
    if (!integer(items[0], 1, INT32_MAX) || !cJSON_IsBool(items[1]) ||
        !cJSON_IsString(items[2]) || strlen(items[2]->valuestring) >= sizeof(saved.host) ||
        !integer(items[3], 1, 65535) || !cJSON_IsString(items[4]) ||
        strlen(items[4]->valuestring) >= sizeof(saved.username) ||
        (items[5] && (!cJSON_IsString(items[5]) || strlen(items[5]->valuestring) >= sizeof(saved.password))) ||
        !integer(items[6], 1, 60)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    gw_ha_settings_t next = saved;
    next.enabled = cJSON_IsTrue(items[1]); next.port = items[3]->valueint; next.period_s = items[6]->valueint;
    strcpy(next.host, items[2]->valuestring); strcpy(next.username, items[4]->valuestring);
    if (items[5]) strcpy(next.password, items[5]->valuestring);
    if (!valid(&next)) { xSemaphoreGive(lock); return ESP_ERR_INVALID_ARG; }
    if (!memcmp(&next, &saved, sizeof(saved))) {
        xSemaphoreGive(lock); *reason = NULL; return ESP_OK;
    }
    if ((uint32_t)items[0]->valueint != saved.revision) {
        xSemaphoreGive(lock); *reason = "settings_conflict"; return ESP_ERR_INVALID_STATE;
    }
    next.revision = saved.revision == INT32_MAX ? 1 : saved.revision + 1;
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "ha_mqtt", &next, sizeof(next));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) saved = next;
    xSemaphoreGive(lock);
    *reason = err == ESP_OK ? NULL : "storage_error";
    return err;
}
