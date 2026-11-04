// SPDX-License-Identifier: BSD-2-Clause
// Deterministic in-memory NVS and serialized mutex substitute for settings
// persistence tests. The actual settings/validation/save code is compiled.
#include "gateway.h"
#include "nvs.h"
#include <string.h>
typedef struct { unsigned char blob[4096]; size_t size; unsigned char auto_refresh[64]; size_t auto_size; unsigned char ha[512]; size_t ha_size; char hostname[64]; char ap_password[64]; int exists; int has_ap_enabled; uint8_t ap_enabled; } storage_t;
static storage_t saved, pending;
static int fail_commit;
void *xSemaphoreCreateMutex(void) { return &saved; }
int xSemaphoreTake(void *lock, unsigned ticks) { (void)lock; (void)ticks; return 1; }
int xSemaphoreGive(void *lock) { (void)lock; return 1; }
void gw_test_nvs_reset(void) { memset(&saved, 0, sizeof(saved)); pending = saved; fail_commit = 0; }
void gw_test_nvs_fail_commit(int fail) { fail_commit = fail; }
void gw_test_nvs_auto_refresh_v1(void) {
    const uint32_t v1[] = {1, 17, 1, 45, 3, 550};
    saved.exists = 1; memcpy(saved.auto_refresh, v1, sizeof(v1)); saved.auto_size = sizeof(v1);
}
size_t gw_test_nvs_blob_size(void) { return saved.size; }
void gw_test_nvs_ap_password(const char *password) { saved.exists = 1; strcpy(saved.ap_password, password); }
void gw_test_nvs_hostname(const char *hostname) { saved.exists = 1; strcpy(saved.hostname, hostname); }
void gw_test_nvs_ap_enabled(int value) { saved.exists = 1; saved.has_ap_enabled = 1; saved.ap_enabled = value; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    if (strcmp(name, "gateway")) return ESP_FAIL;
    if (mode == NVS_READONLY && !saved.exists) return ESP_ERR_NVS_NOT_FOUND;
    pending = saved; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; pending = saved; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *size)
{
    (void)h;
    if (!strcmp(key, "ha_mqtt")) {
        if (!saved.ha_size) return ESP_ERR_NVS_NOT_FOUND;
        if (*size < saved.ha_size) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(value, saved.ha, saved.ha_size); *size = saved.ha_size; return ESP_OK;
    }
    if (!strcmp(key, "auto_refresh")) {
        if (!saved.auto_size) return ESP_ERR_NVS_NOT_FOUND;
        if (*size < saved.auto_size) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(value, saved.auto_refresh, saved.auto_size); *size = saved.auto_size; return ESP_OK;
    }
    if (strcmp(key, "wifi_list") || !saved.size) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < saved.size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(value, saved.blob, saved.size); *size = saved.size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t size)
{
    (void)h;
    if (!strcmp(key, "ha_mqtt")) {
        if (size > sizeof(pending.ha)) return ESP_FAIL;
        memcpy(pending.ha, value, size); pending.ha_size = size; return ESP_OK;
    }
    if (!strcmp(key, "auto_refresh")) {
        if (size > sizeof(pending.auto_refresh)) return ESP_FAIL;
        memcpy(pending.auto_refresh, value, size); pending.auto_size = size; return ESP_OK;
    }
    if (strcmp(key, "wifi_list") || size > sizeof(pending.blob)) return ESP_FAIL;
    memcpy(pending.blob, value, size); pending.size = size; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size)
{
    (void)h;
    const char *text = !strcmp(key, "hostname") ? saved.hostname : !strcmp(key, "ap_password") ? saved.ap_password : "";
    if (!text[0]) return ESP_ERR_NVS_NOT_FOUND;
    size_t needed = strlen(text) + 1;
    if (*size < needed) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(value, text, needed); *size = needed; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    (void)h;
    char *target = !strcmp(key, "hostname") ? pending.hostname : !strcmp(key, "ap_password") ? pending.ap_password : 0;
    if (!target || strlen(value) >= 64) return ESP_FAIL;
    strcpy(target, value); return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value)
{
    (void)h;
    if (strcmp(key, "ap_enabled") || !saved.has_ap_enabled) return ESP_ERR_NVS_NOT_FOUND;
    *value = saved.ap_enabled;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    (void)h;
    if (strcmp(key, "ap_enabled")) return ESP_FAIL;
    pending.has_ap_enabled = 1;
    pending.ap_enabled = value;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fail_commit) return ESP_FAIL;
    pending.exists = 1; saved = pending; return ESP_OK;
}
