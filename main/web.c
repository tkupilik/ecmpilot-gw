// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const char index_html_start[] asm("_binary_index_html_start");
static httpd_handle_t server;

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
    cJSON_AddNumberToObject(j, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(j, "heartbeat", gw_heartbeat_count());
    cJSON_AddNumberToObject(j, "network_cpu", GW_NETWORK_CORE);
    cJSON_AddNumberToObject(j, "application_cpu", GW_APP_CORE);
    cJSON_AddBoolToObject(j, "connected", s.connected);
    cJSON_AddStringToObject(j, "ssid", s.ssid);
    cJSON_AddStringToObject(j, "ip", s.ip);
    cJSON_AddNumberToObject(j, "rssi", s.rssi);
    cJSON_AddNumberToObject(j, "disconnect_reason", s.last_disconnect_reason);
    cJSON_AddBoolToObject(j, "setup_ap", s.setup_ap);
    cJSON_AddStringToObject(j, "ap_ssid", s.ap_ssid);
    return json_response(r, j);
}

static esp_err_t wifi_get(httpd_req_t *r)
{
    gw_settings_t settings;
    gw_settings_get(&settings);
    cJSON *j = cJSON_CreateObject();
    if (!j) return fail(r, "500 Internal Server Error", "Out of memory");
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
    cJSON_Delete(json);
    if (!valid) return fail(r, "400 Bad Request", "Invalid Wi-Fi list (maximum eight unique SSIDs)");
    esp_err_t err = gw_settings_save(&next);
    if (err != ESP_OK) return fail(r, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "500 Internal Server Error", esp_err_to_name(err));
    httpd_resp_set_type(r, "application/json");
    err = httpd_resp_sendstr(r, "{\"saved\":true,\"reconnecting\":true}");
    gw_network_reconfigure();
    return err;
}

static esp_err_t ota_post(httpd_req_t *r)
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

esp_err_t gw_web_start(void)
{
    configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = GW_NETWORK_CORE;
    config.stack_size = 10240;
    config.recv_wait_timeout = 10;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_get},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_get},
        {.uri="/api/wifi", .method=HTTP_GET, .handler=wifi_get},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_post},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(server); server = NULL; return err; }
    }
    return ESP_OK;
}
