// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static SemaphoreHandle_t lock;
static gw_settings_t current = {.version = 1};

static bool valid(const gw_settings_t *s)
{
    if (!s || s->version != 1 || s->count > GW_MAX_NETWORKS) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        const gw_wifi_profile_t *p = &s->networks[i];
        size_t n = strnlen(p->password, sizeof(p->password));
        if (!p->ssid[0] || strnlen(p->ssid, sizeof(p->ssid)) > 32 || n > 64)
            return false;
        if (n && n < 8) return false;
        if (n == 64 && strspn(p->password, "0123456789abcdefABCDEF") != 64)
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(p->ssid, s->networks[j].ssid)) return false;
    }
    return true;
}

esp_err_t gw_settings_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    gw_settings_t saved;
    size_t size = sizeof(saved);
    err = nvs_get_blob(h, "wifi_list", &saved, &size);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(saved) || !valid(&saved)) return ESP_ERR_INVALID_STATE;
    current = saved;
    return ESP_OK;
}

void gw_settings_get(gw_settings_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = current;
    xSemaphoreGive(lock);
}

esp_err_t gw_settings_save(const gw_settings_t *settings)
{
    if (!valid(settings)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "wifi_list", settings, sizeof(*settings));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) current = *settings;
    xSemaphoreGive(lock);
    return err;
}
