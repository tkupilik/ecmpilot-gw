// SPDX-License-Identifier: BSD-2-Clause
#include "gateway.h"
#include "process_data.h"
#include "auto_refresh.h"
#include "ha_settings.h"
#include "gvret.h"
#include "vehicle_data.h"
#include "vehicle_control.h"
#include "power.h"
#include "low_power.h"
#include "log_buffer.h"
#include <atomic>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "pt.h"
#include "runtime.h"

static std::atomic<uint32_t> heartbeat_count;
static_assert(configTIMER_TASK_PRIORITY == configMAX_PRIORITIES - 1,
              "Tmr Svc must have the highest FreeRTOS priority");
static std::atomic<int64_t> restart_at;

class Application final : public Rntm::Process {
    Rntm::Timer timer{this};
    Rntm::Timer control_timer{this};
    struct pt heartbeat_pt;
    int64_t next_heartbeat = 0;
    PT_THREAD(Heartbeat())
    {
        PT_BEGIN(&heartbeat_pt);
        (void)PT_YIELD_FLAG;
        while (true) {
            next_heartbeat = esp_timer_get_time() + 1000000;
            PT_WAIT_UNTIL(&heartbeat_pt, esp_timer_get_time() >= next_heartbeat);
            ++heartbeat_count;
            (void)varstore_write(GW_VAR_HEARTBEAT, (int32_t)heartbeat_count.load());
            (void)varstore_write(GW_VAR_UPTIME_S, (int32_t)(esp_timer_get_time() / 1000000));
            (void)varstore_write(GW_VAR_FREE_HEAP_BYTES, (int32_t)esp_get_free_heap_size());
        }
        PT_END(&heartbeat_pt);
    }
    int Init() override
    {
        configASSERT(xPortGetCoreID() == GW_APP_CORE);
        PT_INIT(&heartbeat_pt);
        gw_vehicle_init();
        timer.Set(SIG_USER, 100000);
        control_timer.Set(SIG_USER + 1, GW_CONTROL_TICK_MS * 1000);
        ESP_LOGI("app", "Hello eCMPilot GW, application CPU%d", xPortGetCoreID());
        return 0;
    }
    int Main(int signal) override
    {
        if (signal == SIG_USER + 1) {
            control_timer.GetTicks();
            gw_vehicle_control_tick();
            return 0;
        }
        if (signal == SIG_USER) {
            timer.GetTicks();
            gw_power_tick();
            gw_vehicle_tick();
            (void)PT_SCHEDULE(Heartbeat());
            const int64_t deadline = restart_at.load();
            if (deadline && esp_timer_get_time() >= deadline) esp_restart();
        }
        return Process::Main(signal);
    }
};

static Application application;
static StackType_t application_stack[4096 / sizeof(StackType_t)];

extern "C" uint32_t gw_heartbeat_count(void) { return heartbeat_count.load(); }
extern "C" void gw_restart_soon(void) { restart_at = esp_timer_get_time() + 1500000; }

extern "C" void app_main(void)
{
    gw_log_start();
    ESP_LOGI("app", "Web log capture ready: %u entries, %u bytes per entry", GW_LOG_CAPACITY, GW_LOG_TEXT_SIZE);
    configASSERT(xPortGetCoreID() == GW_APP_CORE);
    // Do not silently erase saved credentials on an NVS error.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(gw_settings_init());
    ESP_ERROR_CHECK(gw_process_data_init());
    ESP_ERROR_CHECK(gw_auto_refresh_init());
    ESP_ERROR_CHECK(gw_ha_settings_init());
    ESP_ERROR_CHECK(gw_low_power_init());
    ESP_ERROR_CHECK(gw_power_start());
    ESP_ERROR_CHECK(gw_gvret_init());
    ESP_ERROR_CHECK(gw_can_rx_start());
    ESP_ERROR_CHECK(application.Start("application", 4, application_stack,
                                    sizeof(application_stack), GW_APP_CORE));
    ESP_ERROR_CHECK(gw_network_start());
    ESP_ERROR_CHECK(gw_console_start());
    const int64_t deadline = esp_timer_get_time() + 5000000;
    while (gw_heartbeat_count() < 2 && esp_timer_get_time() < deadline)
        vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gw_heartbeat_count() >= 2 ? ESP_OK : ESP_ERR_TIMEOUT);
    // Confirm an OTA image after local startup, independently of AP availability.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
}
