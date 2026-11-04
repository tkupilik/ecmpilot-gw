// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static SemaphoreHandle_t lock;
static gw_settings_t current = {.version = 1};
static char current_hostname[GW_HOSTNAME_SIZE] = GW_DEFAULT_HOSTNAME;
static bool current_ap_enabled;

bool gw_ap_enabled_get(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool enabled = current_ap_enabled;
    xSemaphoreGive(lock);
    return enabled;
}

bool gw_ap_password_valid(const char *password)
{
    if (!password) return false;
    size_t length = strnlen(password, GW_AP_PASSWORD_SIZE);
    if (length < 8 || length >= GW_AP_PASSWORD_SIZE) return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)password[i] < 32 || (unsigned char)password[i] > 126) return false;
    return true;
}
esp_err_t gw_ap_password_get(char password[GW_AP_PASSWORD_SIZE])
{
    password[0] = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t size = GW_AP_PASSWORD_SIZE;
        err = nvs_get_str(h, "ap_password", password, &size);
        if (err == ESP_OK && !gw_ap_password_valid(password)) err = ESP_ERR_INVALID_STATE;
        nvs_close(h);
    }
    xSemaphoreGive(lock);
    if (err != ESP_OK) password[0] = 0;
    return err;
}

static bool alphanumeric(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool gw_hostname_valid(const char *name)
{
    if (!name) return false;
    const size_t length = strnlen(name, GW_HOSTNAME_SIZE);
    if (!length || length >= GW_HOSTNAME_SIZE || !alphanumeric(name[0]) || !alphanumeric(name[length - 1])) return false;
    for (size_t i = 0; i < length; ++i)
        if (!alphanumeric(name[i]) && name[i] != '-') return false;
    return true;
}
static void normalize_hostname(char destination[GW_HOSTNAME_SIZE], const char *source)
{
    size_t i;
    for (i = 0; source[i]; ++i)
        destination[i] = source[i] >= 'A' && source[i] <= 'Z' ? source[i] + ('a' - 'A') : source[i];
    destination[i] = 0;
}

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
    current = (gw_settings_t){.version = 1};
    current_ap_enabled = false;
    memset(current_hostname, 0, sizeof(current_hostname));
    memcpy(current_hostname, GW_DEFAULT_HOSTNAME, sizeof(GW_DEFAULT_HOSTNAME));
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    gw_settings_t saved;
    size_t size = sizeof(saved);
    err = nvs_get_blob(h, "wifi_list", &saved, &size);
    if (err == ESP_OK) {
        if (size != sizeof(saved) || !valid(&saved)) err = ESP_ERR_INVALID_STATE;
        else current = saved;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        char hostname[GW_HOSTNAME_SIZE];
        size = sizeof(hostname);
        err = nvs_get_str(h, "hostname", hostname, &size);
        if (err == ESP_OK) {
            if (!gw_hostname_valid(hostname)) err = ESP_ERR_INVALID_STATE;
            else normalize_hostname(current_hostname, hostname);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) {
        uint8_t enabled = 0;
        err = nvs_get_u8(h, "ap_enabled", &enabled);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        else if (err == ESP_OK && enabled > 1) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) current_ap_enabled = enabled != 0;
    }
    nvs_close(h);
    return err;
}

void gw_settings_get(gw_settings_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = current;
    xSemaphoreGive(lock);
}

esp_err_t gw_settings_save(const gw_settings_t *settings)
{
    return gw_settings_save_network(settings, NULL);
}
void gw_hostname_get(char hostname[GW_HOSTNAME_SIZE])
{
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(hostname, current_hostname, sizeof(current_hostname));
    xSemaphoreGive(lock);
}
esp_err_t gw_settings_save_network(const gw_settings_t *settings, const char *hostname)
{
    return gw_settings_save_wifi(settings, hostname, NULL);
}
esp_err_t gw_settings_save_wifi(const gw_settings_t *settings, const char *hostname, const char *ap_password)
{
    return gw_settings_save_wifi_options(settings, hostname, ap_password, NULL);
}
esp_err_t gw_settings_save_wifi_options(const gw_settings_t *settings, const char *hostname,
                                        const char *ap_password, const bool *ap_enabled)
{
    if (!valid(settings) || (hostname && !gw_hostname_valid(hostname)) ||
        (ap_password && !gw_ap_password_valid(ap_password))) return ESP_ERR_INVALID_ARG;
    char normalized[GW_HOSTNAME_SIZE] = {0};
    if (hostname) normalize_hostname(normalized, hostname);
    xSemaphoreTake(lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open("gateway", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "wifi_list", settings, sizeof(*settings));
        if (err == ESP_OK && hostname) err = nvs_set_str(h, "hostname", normalized);
        if (err == ESP_OK && ap_password) err = nvs_set_str(h, "ap_password", ap_password);
        if (err == ESP_OK && ap_enabled) err = nvs_set_u8(h, "ap_enabled", *ap_enabled ? 1 : 0);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) {
        current = *settings;
        if (hostname) memcpy(current_hostname, normalized, sizeof(current_hostname));
        if (ap_enabled) current_ap_enabled = *ap_enabled;
    }
    xSemaphoreGive(lock);
    return err;
}
