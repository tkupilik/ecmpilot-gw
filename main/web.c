// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "process_data.h"
#include "udp_protocol.h"
#include "log_buffer.h"
#include "retained_log.h"
#include "task_stats.h"
#include "vars_http.h"
#include "power.h"
#include "auto_refresh.h"
#include "low_power.h"
#include "ha_settings.h"
#include "ha_bridge.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const char index_html_start[] asm("_binary_index_html_start");
static httpd_handle_t server;
enum { WEB_MAX_CLIENTS = 4 };
// HTTP internal sockets + clients, GVRET listener/client, UDP, MQTT,
// one DNS socket and one transient/rejected connection.
_Static_assert(CONFIG_LWIP_MAX_SOCKETS >= 3 + WEB_MAX_CLIENTS + 2 + 1 + 1 + 1 + 1,
               "Insufficient shared socket budget for web/GVRET/UDP/MQTT/DNS");

static esp_err_t fail(httpd_req_t *r, const char *status, const char *text)
{
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "text/plain");
    return httpd_resp_sendstr(r, text);
}

static esp_err_t json_response(httpd_req_t *r, cJSON *json)
{
    if (!json) return fail(r, "500 Internal Server Error", "Out of memory");
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return fail(r, "500 Internal Server Error", "Out of memory");
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(r, text);
    free(text);
    return err;
}

// The first prototype is local HTTP, without user accounts. Reject browser
// requests from another origin; this is not authentication of a LAN client.
static bool same_origin(httpd_req_t *r)
{
    char origin[160], host[128], expected[160];
    size_t size = httpd_req_get_hdr_value_len(r, "Origin");
    if (!size) return true; // Also allow console tools such as curl.
    if (size >= sizeof(origin) || httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(r, "Host", host, sizeof(host)) != ESP_OK) return false;
    snprintf(expected, sizeof(expected), "http://%s", host);
    return !strcmp(origin, expected);
}

static esp_err_t index_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(r, index_html_start);
}

static esp_err_t restart_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len != 2) return fail(r, "400 Bad Request", "Expected {}");
    char body[2]; size_t received = 0;
    while (received < sizeof(body)) {
        int n = httpd_req_recv(r, body + received, sizeof(body) - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Restart request interrupted");
        received += n;
    }
    if (memcmp(body, "{}", 2)) return fail(r, "400 Bad Request", "Expected {}");
    ESP_LOGI("web", "Gateway restart requested from web");
    gw_restart_soon(); // CPU1 restarts in 1.5 seconds; finish the HTTP reply first.
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "{\"restarting\":true}");
}

static esp_err_t status_get(httpd_req_t *r)
{
    gw_network_status_t s;
    gw_network_status(&s);
    cJSON *j = cJSON_CreateObject();
    if (!j) return fail(r, "500 Internal Server Error", "Out of memory");
    cJSON_AddStringToObject(j, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(j, "idf", esp_get_idf_version());
    cJSON_AddStringToObject(j, "partition", esp_ota_get_running_partition()->label);
    cJSON_AddNumberToObject(j, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddStringToObject(j, "reset_reason", gw_log_reset_name());
    cJSON_AddNumberToObject(j, "reset_reason_code", gw_log_reset_reason());
    cJSON_AddNumberToObject(j, "rtc_boot_number", gw_retained_log_boot_number());
    cJSON_AddNumberToObject(j, "previous_log_entries", gw_retained_log_count());
    cJSON_AddNumberToObject(j, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(j, "socket_capacity", CONFIG_LWIP_MAX_SOCKETS);
    cJSON_AddNumberToObject(j, "http_max_clients", WEB_MAX_CLIENTS);
    cJSON_AddNumberToObject(j, "heartbeat", gw_heartbeat_count());
    cJSON_AddNumberToObject(j, "udp_port", GW_UDP_PORT);
    cJSON_AddBoolToObject(j, "udp_test_mode", varstore_read(GW_VAR_TEST_ENABLED, 0, 0) == 1);
    cJSON_AddNumberToObject(j, "network_cpu", GW_NETWORK_CORE);
    cJSON_AddNumberToObject(j, "application_cpu", GW_APP_CORE);
    cJSON_AddBoolToObject(j, "connected", s.connected);
    cJSON_AddStringToObject(j, "ssid", s.ssid);
    cJSON_AddStringToObject(j, "ip", s.ip);
    cJSON_AddNumberToObject(j, "rssi", s.rssi);
    cJSON_AddNumberToObject(j, "disconnect_reason", s.last_disconnect_reason);
    cJSON_AddBoolToObject(j, "setup_ap", s.setup_ap);
    cJSON_AddStringToObject(j, "ap_ssid", s.ap_ssid);
    cJSON_AddStringToObject(j, "hostname", s.hostname);
    return json_response(r, j);
}

static esp_err_t vars_page_get(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "/#varstore");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "");
}

static int32_t power_value(size_t id)
{
    varstore_snapshot_t value = varstore_inspect(id, 0);
    return value.raw_valid ? value.raw_value : 0;
}

static esp_err_t auto_refresh_get(httpd_req_t *r)
{
    return json_response(r, gw_auto_refresh_json());
}

static esp_err_t low_power_get(httpd_req_t *r)
{
    return json_response(r, gw_low_power_json());
}

static esp_err_t low_power_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 192)
        return fail(r, "400 Bad Request", "Invalid low-power settings size");
    char body[193]; size_t received = 0;
    while (received < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Settings request interrupted");
        received += n;
    }
    body[received] = 0;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    const char *reason = NULL;
    esp_err_t error = gw_low_power_save(json, &reason);
    cJSON_Delete(json);
    if (error != ESP_OK) return fail(r, error == ESP_ERR_INVALID_ARG ? "400 Bad Request" :
        reason && !strcmp(reason, "settings_conflict") ? "409 Conflict" : "500 Internal Server Error", reason);
    return low_power_get(r);
}

static esp_err_t ha_get(httpd_req_t *r)
{
    cJSON *j = gw_ha_settings_json();
    if (j) {
        cJSON_AddBoolToObject(j, "connected", gw_ha_connected());
        cJSON_AddStringToObject(j, "last_error", esp_err_to_name(gw_ha_last_error()));
    }
    return json_response(r, j);
}

static esp_err_t ha_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 2048)
        return fail(r, "400 Bad Request", "Invalid MQTT settings size");
    char *body = malloc(r->content_len + 1);
    if (!body) return fail(r, "500 Internal Server Error", "Out of memory");
    size_t received = 0;
    while (received < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) { free(body); return fail(r, "408 Request Timeout", "Settings request interrupted"); }
        received += n;
    }
    body[received] = 0;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    free(body);
    const char *reason = NULL;
    esp_err_t err = gw_ha_settings_save(json, &reason);
    cJSON_Delete(json);
    if (err != ESP_OK) return fail(r, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" :
        reason && !strcmp(reason, "settings_conflict") ? "409 Conflict" : "500 Internal Server Error", reason);
    return ha_get(r);
}

static esp_err_t auto_refresh_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 384)
        return fail(r, "400 Bad Request", "Invalid automatic refresh settings size");
    char body[385]; size_t received = 0;
    while (received < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Settings request interrupted");
        received += n;
    }
    body[received] = 0;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    const char *reason = NULL;
    esp_err_t err = gw_auto_refresh_save(json, &reason);
    cJSON_Delete(json);
    if (err != ESP_OK) return fail(r, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" :
        reason && !strcmp(reason, "settings_conflict") ? "409 Conflict" : "500 Internal Server Error", reason);
    return auto_refresh_get(r);
}

static esp_err_t power_get(httpd_req_t *r)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return fail(r, "500 Internal Server Error", "Out of memory");
    const struct { const char *name; size_t id; } fields[] = {
        {"mode", GW_VAR_PWR_MODE}, {"cpu_min_mhz", GW_VAR_PWR_MIN_CPU_MHZ},
        {"cpu_max_mhz", GW_VAR_PWR_MAX_CPU_MHZ}, {"auto_light_sleep", GW_VAR_PWR_AUTO_SLEEP},
        {"light_sleep_count", GW_VAR_PWR_SLEEP_COUNT}, {"light_sleep_ms", GW_VAR_PWR_SLEEP_MS},
        {"wake_cause", GW_VAR_PWR_WAKE_CAUSE}, {"can_enabled", GW_VAR_PWR_CAN_ENABLED},
        {"can_state", GW_VAR_CAN_STATE}, {"last_error", GW_VAR_PWR_LAST_ERROR},
        {"requested", GW_VAR_REQUEST_PWR_GENERATION}, {"completed", GW_VAR_PWR_COMPLETED},
        {"deep_stage", GW_VAR_PWR_DEEP_STAGE}, {"can_suspended", GW_VAR_CAN_SUSPENDED},
        {"network_suspended", GW_VAR_NETWORK_SUSPENDED},
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        cJSON_AddNumberToObject(j, fields[i].name, power_value(fields[i].id));
    cJSON_AddBoolToObject(j, "deep_sleep_available", power_value(GW_VAR_PWR_CAN_ENABLED) != 0);
    return json_response(r, j);
}

static esp_err_t power_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 128)
        return fail(r, "400 Bad Request", "Expected a small power-policy request");
    char body[129];
    size_t received = 0;
    while (received < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Power request interrupted");
        received += n;
    }
    body[received] = 0;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
    int target = -1;
    if (cJSON_IsObject(json) && cJSON_IsString(mode)) {
        if (!strcmp(mode->valuestring, "normal")) target = GW_POWER_NORMAL;
        if (!strcmp(mode->valuestring, "dfs")) target = GW_POWER_DFS;
        if (!strcmp(mode->valuestring, "light_sleep")) target = GW_POWER_LIGHT_SLEEP;
        if (!strcmp(mode->valuestring, "can_deep_sleep")) target = GW_POWER_CAN_DEEP_SLEEP;
    }
    cJSON_Delete(json);
    if (target < 0) return fail(r, "400 Bad Request", "Mode must be normal, dfs, light_sleep or can_deep_sleep");
    esp_err_t error = gw_power_request(target);
    if (error != ESP_OK) return fail(r, "409 Conflict", "A power request is already pending");
    httpd_resp_set_status(r, "202 Accepted");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, "{\"accepted\":true}");
}


static bool log_cursor(const char *text, uint64_t *value)
{
    if (!text[0]) return false;
    *value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || *value > (9007199254740991ULL - (unsigned)(*p - '0')) / 10) return false;
        *value = *value * 10 + (unsigned)(*p - '0');
    }
    return true;
}
static esp_err_t log_get(httpd_req_t *r)
{
    char query[112], value[24], boot[33];
    uint64_t after = 0;
    bool reset = false;
    bool previous_boot = false;
    size_t query_length = httpd_req_get_url_query_len(r);
    if (query_length >= sizeof(query)) return fail(r, "400 Bad Request", "Log query too long");
    if (query_length) {
        if (httpd_req_get_url_query_str(r, query, sizeof(query)) != ESP_OK)
            return fail(r, "400 Bad Request", "Invalid log query");
        esp_err_t err = httpd_query_key_value(query, "after", value, sizeof(value));
        if ((err == ESP_OK && !log_cursor(value, &after)) || (err != ESP_OK && err != ESP_ERR_NOT_FOUND))
            return fail(r, "400 Bad Request", "Invalid log cursor");
        err = httpd_query_key_value(query, "boot", boot, sizeof(boot));
        if (err == ESP_OK) {
            if (strlen(boot) != 32 || strspn(boot, "0123456789abcdef") != 32)
                return fail(r, "400 Bad Request", "Invalid log boot ID");
            reset = strcmp(boot, gw_log_boot_id()) != 0;
            if (reset) after = 0;
        } else if (err != ESP_ERR_NOT_FOUND) return fail(r, "400 Bad Request", "Invalid log boot ID");
        err = httpd_query_key_value(query, "previous", value, sizeof(value));
        if (err == ESP_OK) {
            if (strcmp(value, "0") && strcmp(value, "1"))
                return fail(r, "400 Bad Request", "Invalid previous-boot selector");
            previous_boot = !strcmp(value, "1");
        } else if (err != ESP_ERR_NOT_FOUND) return fail(r, "400 Bad Request", "Invalid previous-boot selector");
    }
    // Only the HTTP server task uses this scratch space. The buffer itself
    // serializes access from logging tasks on both CPUs.
    static gw_log_entry_t entries[GW_LOG_PAGE_SIZE];
    const gw_log_page_t page = previous_boot ?
        gw_retained_log_read(after, entries, GW_LOG_PAGE_SIZE) :
        gw_log_buffer_read(after, entries, GW_LOG_PAGE_SIZE);
    cJSON *json = cJSON_CreateObject();
    if (!json) return fail(r, "500 Internal Server Error", "Out of memory");
    cJSON_AddStringToObject(json, "boot", gw_log_boot_id());
    cJSON_AddBoolToObject(json, "reset", reset);
    cJSON_AddStringToObject(json, "reset_reason", gw_log_reset_name());
    cJSON_AddNumberToObject(json, "reset_reason_code", gw_log_reset_reason());
    cJSON_AddNumberToObject(json, "rtc_boot_number", gw_retained_log_boot_number());
    cJSON_AddBoolToObject(json, "previous", previous_boot);
    cJSON_AddBoolToObject(json, "previous_available", gw_retained_log_count() != 0);
    cJSON_AddNumberToObject(json, "retained_capacity", GW_RETAINED_LOG_CAPACITY);
    cJSON_AddNumberToObject(json, "oldest", (double)page.oldest);
    cJSON_AddNumberToObject(json, "latest", (double)page.latest);
    cJSON_AddNumberToObject(json, "next", (double)page.next);
    cJSON_AddNumberToObject(json, "lost", (double)page.lost);
    cJSON *array = cJSON_AddArrayToObject(json, "entries");
    if (!array) { cJSON_Delete(json); return fail(r, "500 Internal Server Error", "Out of memory"); }
    for (size_t i = 0; i < page.count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) { cJSON_Delete(json); return fail(r, "500 Internal Server Error", "Out of memory"); }
        cJSON_AddNumberToObject(item, "seq", (double)entries[i].sequence);
        cJSON_AddStringToObject(item, "text", entries[i].text);
        cJSON_AddItemToArray(array, item);
    }
    return json_response(r, json);
}

static bool json_integer(const cJSON *item, double minimum, double maximum)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= minimum && item->valuedouble <= maximum &&
           floor(item->valuedouble) == item->valuedouble;
}

static esp_err_t vars_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 512)
        return fail(r, "400 Bad Request", "Expected a small JSON command");
    char body[513];
    size_t received = 0;
    while (received < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Command upload interrupted");
        received += n;
    }
    body[received] = 0;
    cJSON *command = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    if (!command) return fail(r, "400 Bad Request", "Invalid JSON");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(command, "id");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(command, "action");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(command, "value");
    const size_t count = varstore_count();
    if (!count || !cJSON_IsObject(command) || !json_integer(id, 0, count - 1) || !cJSON_IsString(action)) {
        cJSON_Delete(command);
        return fail(r, "400 Bad Request", "Expected an existing integer id and an action string");
    }
    const size_t signal_id = (size_t)id->valuedouble;
    const bool force = !strcmp(action->valuestring, "force");
    const bool write = !strcmp(action->valuestring, "write");
    const bool release = !strcmp(action->valuestring, "release");
    const bool touch = !strcmp(action->valuestring, "touch");
    const bool invalidate = !strcmp(action->valuestring, "invalidate");
    if (!(force || write || release || touch || invalidate)) {
        cJSON_Delete(command);
        return fail(r, "400 Bad Request", "Unknown action");
    }
    if ((write || touch || invalidate) && !gw_process_data_manual(signal_id)) {
        cJSON_Delete(command);
        return fail(r, "403 Forbidden", "Producer-owned signal: use Force or Release");
    }
    if (force || write) {
        if (!json_integer(value, INT32_MIN, INT32_MAX)) {
            cJSON_Delete(command);
            return fail(r, "400 Bad Request", "Value must be a signed 32-bit integer");
        }
        const int32_t number = (int32_t)value->valuedouble;
        if (force) varstore_force(signal_id, true, number);
        else varstore_write(signal_id, number);
    } else if (release) varstore_force(signal_id, false, 0);
    else if (touch) varstore_touch(signal_id);
    else if (invalidate) varstore_invalidate(signal_id);
    cJSON_Delete(command);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "{\"updated\":true}");
}

static esp_err_t wifi_get(httpd_req_t *r)
{
    gw_settings_t settings;
    gw_settings_get(&settings);
    cJSON *j = cJSON_CreateObject();
    if (!j) return fail(r, "500 Internal Server Error", "Out of memory");
    char hostname[GW_HOSTNAME_SIZE];
    gw_hostname_get(hostname);
    cJSON_AddStringToObject(j, "hostname", hostname);
    char ap_password[GW_AP_PASSWORD_SIZE];
    esp_err_t password_error = gw_ap_password_get(ap_password);
    if (password_error != ESP_OK) { cJSON_Delete(j); return fail(r, "500 Internal Server Error", "Cannot read setup AP password"); }
    gw_network_status_t status;
    gw_network_status(&status);
    cJSON_AddStringToObject(j, "ap_ssid", status.ap_ssid);
    cJSON_AddStringToObject(j, "ap_password", ap_password);
    cJSON_AddBoolToObject(j, "ap_enabled", gw_ap_enabled_get());
    cJSON *networks = cJSON_AddArrayToObject(j, "networks");
    if (!networks) { cJSON_Delete(j); return fail(r, "500 Internal Server Error", "Out of memory"); }
    for (unsigned i = 0; i < settings.count; ++i) {
        cJSON *p = cJSON_CreateObject();
        if (!p) { cJSON_Delete(j); return fail(r, "500 Internal Server Error", "Out of memory"); }
        cJSON_AddStringToObject(p, "ssid", settings.networks[i].ssid);
        cJSON_AddBoolToObject(p, "has_password", settings.networks[i].password[0] != 0);
        cJSON_AddItemToArray(networks, p);
    }
    return json_response(r, j);
}

static esp_err_t wifi_post(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    if (r->content_len <= 0 || r->content_len > 4096) return fail(r, "400 Bad Request", "Invalid settings size");
    char *body = calloc(1, r->content_len + 1);
    if (!body) return fail(r, "500 Internal Server Error", "Out of memory");
    int used = 0;
    while (used < r->content_len) {
        int n = httpd_req_recv(r, body + used, r->content_len - used);
        if (n <= 0) { free(body); return fail(r, "408 Request Timeout", "Settings upload interrupted"); }
        used += n;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return fail(r, "400 Bad Request", "Invalid JSON");
    cJSON *array = cJSON_GetObjectItemCaseSensitive(json, "networks");
    cJSON *hostname = cJSON_GetObjectItemCaseSensitive(json, "hostname");
    cJSON *ap_password = cJSON_GetObjectItemCaseSensitive(json, "ap_password");
    cJSON *ap_enabled = cJSON_GetObjectItemCaseSensitive(json, "ap_enabled");
    if (ap_enabled && !cJSON_IsBool(ap_enabled)) {
        cJSON_Delete(json);
        return fail(r, "400 Bad Request", "ap_enabled must be true or false");
    }
    bool enable_ap = cJSON_IsTrue(ap_enabled);
    if (ap_password && (!cJSON_IsString(ap_password) || !gw_ap_password_valid(ap_password->valuestring))) {
        cJSON_Delete(json);
        return fail(r, "400 Bad Request", "AP password must be 8-63 printable ASCII characters");
    }
    if (hostname && (!cJSON_IsString(hostname) || !gw_hostname_valid(hostname->valuestring))) {
        cJSON_Delete(json);
        return fail(r, "400 Bad Request", "Hostname must be 1-32 letters, digits or hyphens, starting and ending with a letter or digit");
    }
    gw_settings_t old, next = {.version = 1};
    gw_settings_get(&old);
    bool valid = cJSON_IsArray(array) && cJSON_GetArraySize(array) <= GW_MAX_NETWORKS;
    cJSON *p;
    if (valid) cJSON_ArrayForEach(p, array) {
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(p, "ssid");
        cJSON *password = cJSON_GetObjectItemCaseSensitive(p, "password");
        if (!cJSON_IsString(ssid) || !ssid->valuestring[0] || strlen(ssid->valuestring) > 32 ||
            (password && (!cJSON_IsString(password) || strlen(password->valuestring) > 64))) {
            valid = false;
            break;
        }
        gw_wifi_profile_t *profile = &next.networks[next.count++];
        strlcpy(profile->ssid, ssid->valuestring, sizeof(profile->ssid));
        if (password) strlcpy(profile->password, password->valuestring, sizeof(profile->password));
        else for (unsigned i = 0; i < old.count; ++i)
            if (!strcmp(profile->ssid, old.networks[i].ssid))
                strlcpy(profile->password, old.networks[i].password, sizeof(profile->password));
    }
    if (!valid) { cJSON_Delete(json); return fail(r, "400 Bad Request", "Invalid Wi-Fi list (maximum eight unique SSIDs)"); }
    char previous_ap_password[GW_AP_PASSWORD_SIZE];
    const bool restart = ap_password &&
        (gw_ap_password_get(previous_ap_password) != ESP_OK || strcmp(previous_ap_password, ap_password->valuestring));
    esp_err_t err = gw_settings_save_wifi_options(&next, hostname ? hostname->valuestring : NULL,
                                         ap_password ? ap_password->valuestring : NULL,
                                         ap_enabled ? &enable_ap : NULL);
    cJSON_Delete(json);
    if (err != ESP_OK) return fail(r, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "500 Internal Server Error", esp_err_to_name(err));
    httpd_resp_set_type(r, "application/json");
    err = httpd_resp_sendstr(r, restart ? "{\"saved\":true,\"restarting\":true}" : "{\"saved\":true,\"reconnecting\":true}");
    if (restart) gw_restart_soon();
    else gw_network_reconfigure();
    return err;
}

static esp_err_t ota_upload(httpd_req_t *r)
{
    if (!same_origin(r)) return fail(r, "403 Forbidden", "Origin rejected");
    char content_type[64];
    if (httpd_req_get_hdr_value_str(r, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream"))
        return fail(r, "415 Unsupported Media Type", "Upload the application .bin as application/octet-stream");
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition || r->content_len < 288 || (size_t)r->content_len > partition->size)
        return fail(r, "400 Bad Request", "Image does not fit the inactive OTA slot");
    unsigned char buffer[2048];
    // Read the first complete image header and app description before erasing flash.
    const size_t prefix = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    size_t received = 0;
    while (received < prefix) {
        int n = httpd_req_recv(r, (char *)buffer + received, prefix - received);
        if (n <= 0) return fail(r, "408 Request Timeout", "Image header interrupted");
        received += n;
    }
    esp_app_desc_t desc;
    memcpy(&desc, buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(desc));
    if (buffer[0] != ESP_IMAGE_HEADER_MAGIC || desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        memcmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)))
        return fail(r, "400 Bad Request", "Expected an ecmpilot_gw application image");
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(partition, r->content_len, &ota);
    if (err != ESP_OK) return fail(r, "500 Internal Server Error", esp_err_to_name(err));
    err = esp_ota_write(ota, buffer, received);
    int timeouts = 0;
    while (err == ESP_OK && received < (size_t)r->content_len) {
        size_t remain = r->content_len - received;
        int n = httpd_req_recv(r, (char *)buffer, remain < sizeof(buffer) ? remain : sizeof(buffer));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 4) continue;
        if (n <= 0) { err = ESP_ERR_TIMEOUT; break; }
        timeouts = 0;
        err = esp_ota_write(ota, buffer, n);
        received += n;
    }
    if (err != ESP_OK) {
        esp_ota_abort(ota);
        return fail(r, "400 Bad Request", "Upload failed; the boot partition is unchanged");
    }
    err = esp_ota_end(ota); // Validates the complete image, including chip compatibility.
    if (err != ESP_OK) return fail(r, "400 Bad Request", esp_err_to_name(err));
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) return fail(r, "500 Internal Server Error", esp_err_to_name(err));
    httpd_resp_set_type(r, "application/json");
    err = httpd_resp_sendstr(r, "{\"updated\":true,\"restarting\":true}");
    gw_restart_soon();
    return err;
}

static esp_err_t ota_post(httpd_req_t *r)
{
    varstore_write(GW_VAR_OTA_ACTIVE, 1);
    esp_err_t error = ota_upload(r);
    varstore_write(GW_VAR_OTA_ACTIVE, 0);
    return error;
}

esp_err_t gw_web_start(void)
{
    configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = GW_NETWORK_CORE;
    config.stack_size = 10240;
    config.recv_wait_timeout = 10;
    // HTTP uses another three internal/control sockets. Leave space for
    // GVRET listener+client, UDP, MQTT, DNS and transient/rejected connections.
    config.max_open_sockets = WEB_MAX_CLIENTS;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 19;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_get},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_get},
        {.uri="/api/restart", .method=HTTP_POST, .handler=restart_post},
        {.uri="/api/power", .method=HTTP_GET, .handler=power_get},
        {.uri="/api/power", .method=HTTP_POST, .handler=power_post},
        {.uri="/api/auto-refresh", .method=HTTP_GET, .handler=auto_refresh_get},
        {.uri="/api/auto-refresh", .method=HTTP_POST, .handler=auto_refresh_post},
        {.uri="/api/low-power", .method=HTTP_GET, .handler=low_power_get},
        {.uri="/api/low-power", .method=HTTP_POST, .handler=low_power_post},
        {.uri="/api/ha", .method=HTTP_GET, .handler=ha_get},
        {.uri="/api/ha", .method=HTTP_POST, .handler=ha_post},
        {.uri="/api/wifi", .method=HTTP_GET, .handler=wifi_get},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_post},
        {.uri="/vars", .method=HTTP_GET, .handler=vars_page_get},
        {.uri="/api/vars", .method=HTTP_GET, .handler=gw_vars_get},
        {.uri="/api/vars", .method=HTTP_POST, .handler=vars_post},
        {.uri="/api/log", .method=HTTP_GET, .handler=log_get},
        {.uri="/api/tasks", .method=HTTP_GET, .handler=gw_tasks_get},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(server); server = NULL; return err; }
    }
    return ESP_OK;
}
