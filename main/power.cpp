// SPDX-License-Identifier: BSD-2-Clause
// Adapted from the project's LCD power bench; no LCD or CAN lifecycle calls.
#include "power_policy.h"
#include "process_data.h"
#include "can_config.h"
#include "can_pins.h"
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rtc_io.h"
#include <atomic>
#include <climits>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_private/esp_sleep_internal.h"
#include "esp_timer.h"

#if !CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE || !CONFIG_ESP_SLEEP_DEBUG
#error "GW power measurement requires PM, tickless idle and sleep-result diagnostics"
#endif

namespace {
std::atomic<uint32_t> sleep_count, sleep_us;
esp_sleep_context_t sleep_context;
int64_t next_publish;
uint32_t last_sleep_us;
uint64_t total_sleep_us;

esp_err_t IRAM_ATTR on_sleep_enter(int64_t, void *)
{
    sleep_context.sleep_request_result = ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t IRAM_ATTR on_sleep_exit(int64_t us, void *)
{
    // IDF also calls exit after skipped/rejected attempts. Count only success.
    if (us > 0 && sleep_context.sleep_request_result == ESP_OK) {
        sleep_count.fetch_add(1, std::memory_order_relaxed);
        sleep_us.fetch_add((uint32_t)us, std::memory_order_relaxed);
    }
    return ESP_OK;
}
}

extern "C" esp_err_t gw_power_apply_profile(int minimum_mhz, int maximum_mhz, bool light_sleep)
{
    esp_pm_config_t config = {maximum_mhz, minimum_mhz, light_sleep};
    esp_err_t error = esp_pm_configure(&config);
    if (error == ESP_OK)
        ESP_LOGI("power", "CPU policy %d..%d MHz; auto light sleep=%d; CAN lifecycle unchanged",
                 minimum_mhz, maximum_mhz, light_sleep);
    else
        ESP_LOGW("power", "PM configuration: %s", esp_err_to_name(error));
    return error;
}

extern "C" esp_err_t gw_power_start(void)
{
    // Deep sleep retains the recessive TX level. Restore digital routing before
    // the CAN owner installs TWAI; set the output latch before releasing hold.
    rtc_gpio_deinit(GW_CAN_TX_GPIO);
    rtc_gpio_deinit(GW_CAN_RX_GPIO);
    gpio_reset_pin(GW_CAN_TX_GPIO);
    gpio_set_level(GW_CAN_TX_GPIO, 1);
    gpio_set_direction(GW_CAN_TX_GPIO, GPIO_MODE_OUTPUT);
    gpio_hold_dis(GW_CAN_TX_GPIO);
    gpio_deep_sleep_hold_dis();
    ESP_LOGI("power", "Wake cause: %d%s", esp_sleep_get_wakeup_cause(),
             esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? " (CAN RX)" : "");
    esp_pm_sleep_cbs_register_config_t callbacks = {};
    callbacks.enter_cb = on_sleep_enter;
    callbacks.exit_cb = on_sleep_exit;
    esp_sleep_set_sleep_context(&sleep_context);
    esp_err_t error = esp_pm_light_sleep_register_cbs(&callbacks);
    if (error != ESP_OK) return error;
    error = gw_power_policy_init();
    if (error != ESP_OK) {
        esp_pm_light_sleep_unregister_cbs(&callbacks);
        return error;
    }
    varstore_write(GW_VAR_PWR_SLEEP_COUNT, 0);
    varstore_write(GW_VAR_PWR_SLEEP_MS, 0);
    varstore_write(GW_VAR_PWR_WAKE_CAUSE, esp_sleep_get_wakeup_cause());
    varstore_write(GW_VAR_PWR_CAN_ENABLED, GW_CAN_ENABLED);
    next_publish = 0;
    return ESP_OK;
}

extern "C" void gw_power_tick(void)
{
    gw_power_policy_tick();
    int64_t now = esp_timer_get_time();
    if (now < next_publish) return;
    next_publish = now + 1000000;
    uint32_t elapsed = sleep_us.load(std::memory_order_relaxed);
    total_sleep_us += (uint32_t)(elapsed - last_sleep_us);
    last_sleep_us = elapsed;
    varstore_write(GW_VAR_PWR_SLEEP_COUNT, sleep_count.load(std::memory_order_relaxed) & INT32_MAX);
    // Convert after accumulation, preserving sub-ms sleeps and counter wrap.
    varstore_write(GW_VAR_PWR_SLEEP_MS, (total_sleep_us / 1000) & INT32_MAX);
}

extern "C" esp_err_t gw_power_enter_can_deep_sleep(void)
{
    configASSERT(xPortGetCoreID() == GW_APP_CORE);
    // Fail closed: only raw acknowledgements from the two peripheral owners
    // authorize entry. A diagnostic Force must not bypass lifecycle hand-off.
    const auto can = varstore_inspect(GW_VAR_CAN_SUSPENDED, 0);
    const auto net = varstore_inspect(GW_VAR_NETWORK_SUSPENDED, 0);
    if (!GW_CAN_ENABLED || !can.raw_valid || can.raw_value != 1 ||
        !net.raw_valid || net.raw_value != 1)
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (error != ESP_OK) return error;
    // Keep the powered transceiver's TXD recessive throughout sleep. RXD stays
    // powered and connects to RTC GPIO16; only a LOW level may wake the chip.
    gpio_set_level(GW_CAN_TX_GPIO, 1);
    error = gpio_set_direction(GW_CAN_TX_GPIO, GPIO_MODE_OUTPUT);
    if (error != ESP_OK) return error;
    error = gpio_set_direction(GW_CAN_RX_GPIO, GPIO_MODE_INPUT);
    if (error != ESP_OK) return error;
    if (gpio_get_level(GW_CAN_RX_GPIO) == 0) {
        ESP_LOGW("power", "Deep sleep cancelled: CAN RX is dominant already");
        return ESP_ERR_INVALID_STATE;
    }
    error = esp_sleep_enable_ext0_wakeup(GW_CAN_RX_GPIO, 0);
    if (error != ESP_OK) return error;
    error = gpio_hold_en(GW_CAN_TX_GPIO);
    if (error != ESP_OK) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
        return error;
    }
    gpio_deep_sleep_hold_en();
    ESP_LOGI("power", "Entering CAN-only deep sleep: RX GPIO%d LOW wakes; no timer; TX GPIO%d held recessive",
             GW_CAN_RX_GPIO, GW_CAN_TX_GPIO);
    esp_deep_sleep_start();
    return ESP_FAIL; // Defensive only: the SDK entry point does not return.
}
