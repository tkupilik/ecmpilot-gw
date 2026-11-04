// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "gvret.h"
#include "ha_bridge.h"
#include "process_data.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "runtime.h"

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static gw_network_status_t status;
static SemaphoreHandle_t startup_done;
static esp_err_t startup_result;

enum EventType { Reconfigure, ScanDone, Disconnected, GotIp, LostIp };
struct NetworkEvent { EventType type; int reason; esp_ip4_addr_t ip; };

class Network final : public Rntm::Process {
    QueueHandle_t events = nullptr;
    Rntm::Timer timer{this};
    gw_settings_t settings{};
    esp_netif_t *station = nullptr;
    esp_netif_t *access_point = nullptr;
    int selected = -1;
    bool power_requested = false;
    bool power_suspended = false;
    bool scanning = false;
    bool connecting = false;
    bool connected = false;
    bool setup_ap = false;
    bool setup_ap_allowed = false;
    int64_t scan_at = 0;
    int64_t connect_at = 0;
    int64_t offline_since = 0;
    int64_t blocked_until[GW_MAX_NETWORKS]{};

    static void OnEvent(void *ctx, esp_event_base_t base, int32_t id, void *data)
    {
        configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
        auto *self = static_cast<Network *>(ctx);
        NetworkEvent event{};
        if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) event.type = ScanDone;
        else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            event.type = Disconnected;
            event.reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            event.type = GotIp;
            event.ip = static_cast<ip_event_got_ip_t *>(data)->ip_info.ip;
        } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
            event.type = LostIp;
        } else return;
        if (xQueueSend(self->events, &event, 0) != pdPASS) {
            ESP_LOGE("network", "Event queue full; restarting network state");
            self->SendSig(SIG_USER + 2);
        } else self->SendSig(SIG_USER);
    }

    esp_err_t ApplyHostname()
    {
        char hostname[GW_HOSTNAME_SIZE];
        gw_hostname_get(hostname);
        ESP_RETURN_ON_ERROR(esp_netif_set_hostname(station, hostname), "network", "station hostname");
        ESP_RETURN_ON_ERROR(esp_netif_set_hostname(access_point, hostname), "network", "AP hostname");
        portENTER_CRITICAL(&status_lock);
        strlcpy(status.hostname, hostname, sizeof(status.hostname));
        portEXIT_CRITICAL(&status_lock);
        return ESP_OK;
    }

    esp_err_t InitWifi()
    {
        configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
        ESP_RETURN_ON_ERROR(esp_netif_init(), "network", "netif init");
        ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), "network", "event loop");
        station = esp_netif_create_default_wifi_sta();
        access_point = esp_netif_create_default_wifi_ap();
        if (!station || !access_point) return ESP_ERR_NO_MEM;
        ESP_RETURN_ON_ERROR(ApplyHostname(), "network", "startup hostname");
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        init.wifi_task_core_id = GW_NETWORK_CORE;
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init), "network", "wifi init");
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), "network", "storage");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnEvent, this), "network", "wifi events");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, OnEvent, this), "network", "ip events");
        uint8_t mac[6];
        ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), "network", "mac");
        wifi_config_t ap{};
        snprintf(reinterpret_cast<char *>(ap.ap.ssid), sizeof(ap.ap.ssid),
                 "eCMPilot-%02X%02X%02X", mac[3], mac[4], mac[5]);
        char password[GW_AP_PASSWORD_SIZE]{};
        nvs_handle_t nvs;
        ESP_RETURN_ON_ERROR(nvs_open("gateway", NVS_READWRITE, &nvs), "network", "ap nvs");
        size_t size = sizeof(password);
        esp_err_t err = nvs_get_str(nvs, "ap_password", password, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            for (unsigned i = 0; i < 16; ++i) password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
            err = nvs_set_str(nvs, "ap_password", password);
            if (err == ESP_OK) err = nvs_commit(nvs);
        }
        nvs_close(nvs);
        if (err != ESP_OK) return err;
        if (!gw_ap_password_valid(password)) return ESP_ERR_INVALID_STATE;
        memcpy(ap.ap.password, password, strlen(password));
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ap.ap.max_connection = 2;
        ap.ap.channel = 1;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), "network", "mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), "network", "ap config");
        portENTER_CRITICAL(&status_lock);
        strlcpy(status.ap_ssid, reinterpret_cast<char *>(ap.ap.ssid), sizeof(status.ap_ssid));
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI("network", "Setup AP: %s", ap.ap.ssid);
        // Keep the serial credential announcement outside the web log capture.
        printf("Setup AP password: %s\n", password);
        gw_settings_get(&settings);
        setup_ap_allowed = gw_ap_enabled_get();
        setup_ap = setup_ap_allowed && settings.count == 0;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(setup_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA), "network", "startup mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), "network", "start");
        ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_MIN_MODEM), "network", "power save");
        portENTER_CRITICAL(&status_lock);
        status.setup_ap = setup_ap;
        portEXIT_CRITICAL(&status_lock);
        offline_since = esp_timer_get_time();
        ESP_RETURN_ON_ERROR(gw_web_start(), "network", "web start");
        ESP_RETURN_ON_ERROR(gw_udp_start(), "network", "UDP start");
        esp_err_t gvret_error = gw_gvret_start();
        if (gvret_error != ESP_OK) ESP_LOGW("network", "GVRET unavailable: %s", esp_err_to_name(gvret_error));
        esp_err_t ha_error = gw_ha_start();
        if (ha_error != ESP_OK) ESP_LOGW("network", "HA unavailable: %s", esp_err_to_name(ha_error));
        return ESP_OK;
    }

    void EnableSetupAp()
    {
        if (!setup_ap_allowed || setup_ap) return;
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return;
        setup_ap = true;
        portENTER_CRITICAL(&status_lock);
        status.setup_ap = true;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI("network", "Setup AP enabled at http://192.168.4.1/");
    }

    void DisableSetupAp()
    {
        if (!setup_ap) return;
        esp_err_t error = esp_wifi_set_mode(WIFI_MODE_STA);
        if (error != ESP_OK) {
            ESP_LOGW("network", "Disable setup AP: %s", esp_err_to_name(error));
            return; // Keep actual state visible; the timer retries disabling it.
        }
        setup_ap = false;
        portENTER_CRITICAL(&status_lock);
        status.setup_ap = false;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI("network", "Setup AP disabled");
    }

    void Scan()
    {
        if (connected || connecting || scanning || settings.count == 0) return;
        const esp_err_t err = esp_wifi_scan_start(nullptr, false);
        scanning = err == ESP_OK;
        scan_at = esp_timer_get_time() + 15000000;
        if (err != ESP_OK) ESP_LOGW("network", "Scan: %s", esp_err_to_name(err));
    }

    void SelectNetwork()
    {
        scanning = false;
        uint16_t count = 0;
        if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || !count) {
            esp_wifi_clear_ap_list();
            return;
        }
        auto *records = static_cast<wifi_ap_record_t *>(calloc(count, sizeof(wifi_ap_record_t)));
        if (!records) { esp_wifi_clear_ap_list(); return; }
        if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) { free(records); return; }
        selected = -1;
        int chosen_ap = -1;
        const int64_t now = esp_timer_get_time();
        for (unsigned p = 0; p < settings.count; ++p) {
            if (blocked_until[p] > now) continue;
            for (unsigned a = 0; a < count; ++a) {
                if (!strcmp(settings.networks[p].ssid, reinterpret_cast<char *>(records[a].ssid)) &&
                    (chosen_ap < 0 || records[a].rssi > records[chosen_ap].rssi)) {
                    selected = p;
                    chosen_ap = a;
                }
            }
            if (selected >= 0) break;
        }
        free(records);
        if (selected < 0) return;
        wifi_config_t cfg{};
        const auto &profile = settings.networks[selected];
        memcpy(cfg.sta.ssid, profile.ssid, strlen(profile.ssid));
        memcpy(cfg.sta.password, profile.password, strlen(profile.password));
        cfg.sta.threshold.authmode = profile.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        cfg.sta.pmf_cfg.capable = true;
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (err == ESP_OK) err = esp_wifi_connect();
        if (err == ESP_OK) {
            connecting = true;
            connect_at = now;
            portENTER_CRITICAL(&status_lock);
            strlcpy(status.ssid, profile.ssid, sizeof(status.ssid));
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI("network", "Connecting to %s", profile.ssid);
        } else {
            blocked_until[selected] = now + 60000000;
            ESP_LOGW("network", "Connect: %s", esp_err_to_name(err));
        }
    }

    void Reload()
    {
        setup_ap_allowed = gw_ap_enabled_get();
        if (!setup_ap_allowed) DisableSetupAp();
        if (scanning) esp_wifi_scan_stop();
        esp_wifi_clear_ap_list();
        scanning = false;
        esp_wifi_disconnect();
        connected = connecting = false;
        selected = -1;
        memset(blocked_until, 0, sizeof(blocked_until));
        gw_settings_get(&settings);
        scan_at = esp_timer_get_time() + 1000000;
        offline_since = esp_timer_get_time();
        const esp_err_t hostname_error = ApplyHostname();
        if (hostname_error != ESP_OK) ESP_LOGE("network", "Hostname update: %s", esp_err_to_name(hostname_error));
        portENTER_CRITICAL(&status_lock);
        status.connected = false;
        status.ip[0] = 0;
        status.ssid[0] = 0;
        portEXIT_CRITICAL(&status_lock);
        if (!settings.count) EnableSetupAp();
    }

    bool PowerStep()
    {
        const auto request = varstore_inspect(GW_VAR_REQUEST_NETWORK_SUSPEND, 0);
        bool suspend = request.raw_valid && request.raw_value == 1;
        if (suspend != power_requested) {
            power_requested = suspend;
            esp_err_t error = ESP_OK;
            if (suspend) {
                if (scanning) esp_wifi_scan_stop();
                esp_wifi_clear_ap_list();
                error = esp_wifi_stop();
                if (error == ESP_ERR_WIFI_NOT_STARTED) error = ESP_OK;
                if (error == ESP_OK) power_suspended = true;
            } else if (power_suspended) {
                error = esp_wifi_start();
                if (error == ESP_OK) power_suspended = false;
            }
            connected = connecting = scanning = false;
            selected = -1;
            xQueueReset(events);
            memset(blocked_until, 0, sizeof(blocked_until));
            scan_at = offline_since = esp_timer_get_time();
            portENTER_CRITICAL(&status_lock);
            status.connected = false;
            status.setup_ap = !power_suspended && setup_ap;
            status.ip[0] = 0;
            portEXIT_CRITICAL(&status_lock);
            varstore_write(GW_VAR_WIFI_CONNECTED, 0);
            varstore_invalidate(GW_VAR_WIFI_RSSI_DBM);
            varstore_write(GW_VAR_NETWORK_SUSPEND_ERROR, error);
            varstore_write(GW_VAR_NETWORK_SUSPENDED, power_suspended);
            varstore_write(GW_VAR_NETWORK_SUSPEND_COMPLETED, suspend);
            ESP_LOGI("network", "Wi-Fi power %s: %s", suspend ? "stop" : "resume", esp_err_to_name(error));
        }
        return suspend || power_suspended;
    }

    int Init() override
    {
        varstore_write(GW_VAR_NETWORK_SUSPENDED, 0);
        varstore_write(GW_VAR_NETWORK_SUSPEND_ERROR, ESP_OK);
        varstore_write(GW_VAR_NETWORK_SUSPEND_COMPLETED, 0);
        startup_result = InitWifi();
        xSemaphoreGive(startup_done);
        if (startup_result != ESP_OK) return 1;
        timer.Set(SIG_USER + 1, 1000000);
        return 0;
    }

    int Main(int signal) override
    {
        configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
        if (signal == SIG_USER + 1) timer.GetTicks();
        if (PowerStep()) return 0;
        if (signal == SIG_USER + 2) Reload();
        if (signal == SIG_USER) {
            NetworkEvent event;
            while (xQueueReceive(events, &event, 0) == pdPASS) {
                if (event.type == Reconfigure) Reload();
                else if (event.type == ScanDone && scanning) SelectNetwork();
                else if (event.type == Disconnected) {
                    if (selected >= 0) blocked_until[selected] = esp_timer_get_time() + 60000000;
                    if (connected) offline_since = esp_timer_get_time();
                    connected = connecting = false;
                    selected = -1;
                    scan_at = esp_timer_get_time() + 1000000;
                    portENTER_CRITICAL(&status_lock);
                    status.connected = false;
                    status.ip[0] = 0;
                    status.last_disconnect_reason = event.reason;
                    portEXIT_CRITICAL(&status_lock);
                } else if (event.type == LostIp && connected) {
                    esp_wifi_disconnect();
                } else if (event.type == GotIp) {
                    connected = true;
                    connecting = false;
                    portENTER_CRITICAL(&status_lock);
                    status.connected = true;
                    snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&event.ip));
                    status.last_disconnect_reason = 0;
                    portEXIT_CRITICAL(&status_lock);
                    ESP_LOGI("network", "Web UI: http://" IPSTR "/", IP2STR(&event.ip));
                }
            }
        }
        if (signal == SIG_USER + 1) {
            const int64_t now = esp_timer_get_time();
            if (!setup_ap_allowed) DisableSetupAp();
            else if (!connected && now - offline_since >= 30000000) EnableSetupAp();
            if (connecting && now - connect_at >= 20000000) esp_wifi_disconnect();
            if (now >= scan_at) Scan();
            if (connected) {
                wifi_ap_record_t ap{};
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    portENTER_CRITICAL(&status_lock);
                    status.rssi = ap.rssi;
                    portEXIT_CRITICAL(&status_lock);
                    (void)varstore_write(GW_VAR_WIFI_RSSI_DBM, ap.rssi);
                }
            } else {
                (void)varstore_invalidate(GW_VAR_WIFI_RSSI_DBM);
            }
            (void)varstore_write(GW_VAR_WIFI_CONNECTED, connected ? 1 : 0);
        }
        return Process::Main(signal);
    }
public:
    esp_err_t Launch()
    {
        events = xQueueCreate(16, sizeof(NetworkEvent));
        if (!events) return ESP_ERR_NO_MEM;
        return Start("network", 5, nullptr, 6144, GW_NETWORK_CORE);
    }
    void ReconfigureAsync()
    {
        NetworkEvent event{Reconfigure, 0, {0}};
        if (xQueueSend(events, &event, 0) == pdPASS) SendSig(SIG_USER);
        else SendSig(SIG_USER + 2);
    }
};

static Network network;
extern "C" esp_err_t gw_network_start(void)
{
    startup_done = xSemaphoreCreateBinary();
    if (!startup_done) return ESP_ERR_NO_MEM;
    esp_err_t err = network.Launch();
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(startup_done, pdMS_TO_TICKS(15000)) != pdPASS) return ESP_ERR_TIMEOUT;
    return startup_result;
}
extern "C" void gw_network_reconfigure(void) { network.ReconfigureAsync(); }
extern "C" void gw_network_status(gw_network_status_t *out)
{
    portENTER_CRITICAL(&status_lock);
    *out = status;
    portEXIT_CRITICAL(&status_lock);
}
