// SPDX-License-Identifier: BSD-2-Clause
#include "power_policy.h"
#include "low_power.h"
#include "can_config.h"
#include "process_data.h"
#include "esp_timer.h"
#include <limits.h>
#include <stdatomic.h>

static int32_t waiting, target;
static int64_t apply_at, deadline;
static int deep_stage;
static atomic_flag request_lock = ATOMIC_FLAG_INIT;
static bool automatic_request;

static int32_t producer(size_t id)
{
    varstore_snapshot_t value = varstore_inspect(id, 0);
    return value.raw_valid ? value.raw_value : 0;
}

static esp_err_t apply(int mode)
{
    int minimum = mode == GW_POWER_NORMAL ? 160 : 40;
    int maximum = mode == GW_POWER_LIGHT_SLEEP ? 80 : 160;
    bool light_sleep = mode == GW_POWER_LIGHT_SLEEP;
    esp_err_t error = gw_power_apply_profile(minimum, maximum, light_sleep);
    if (error == ESP_OK) {
        varstore_write(GW_VAR_PWR_MIN_CPU_MHZ, minimum);
        varstore_write(GW_VAR_PWR_MAX_CPU_MHZ, maximum);
        varstore_write(GW_VAR_PWR_AUTO_SLEEP, light_sleep);
        varstore_write(GW_VAR_PWR_MODE, mode);
    }
    return error;
}

esp_err_t gw_power_policy_init(void)
{
    waiting = target = 0;
    apply_at = deadline = 0;
    deep_stage = GW_DEEP_IDLE;
    automatic_request = false;
    atomic_flag_clear(&request_lock);
    varstore_write(GW_VAR_PWR_DEEP_STAGE, GW_DEEP_IDLE);
    varstore_write(GW_VAR_REQUEST_CAN_SUSPEND, 0);
    varstore_write(GW_VAR_REQUEST_NETWORK_SUSPEND, 0);
    varstore_write(GW_VAR_REQUEST_PWR_MODE, GW_POWER_NORMAL);
    varstore_write(GW_VAR_REQUEST_PWR_GENERATION, 0);
    varstore_write(GW_VAR_PWR_COMPLETED, 0);
    esp_err_t error = apply(GW_POWER_NORMAL);
    varstore_write(GW_VAR_PWR_LAST_ERROR, error);
    return error;
}

static esp_err_t request(int mode, bool automatic)
{
    if (mode < GW_POWER_NORMAL || mode > GW_POWER_CAN_DEEP_SLEEP) return ESP_ERR_INVALID_ARG;
    if (mode == GW_POWER_CAN_DEEP_SLEEP && (!GW_CAN_ENABLED ||
        producer(GW_VAR_CAN_SUSPEND_COMPLETED) || producer(GW_VAR_NETWORK_SUSPEND_COMPLETED)))
        return ESP_ERR_INVALID_STATE;
    int32_t generation = producer(GW_VAR_REQUEST_PWR_GENERATION);
    if (generation != producer(GW_VAR_PWR_COMPLETED)) return ESP_ERR_INVALID_STATE;
    automatic_request = automatic;
    varstore_write(GW_VAR_REQUEST_PWR_MODE, mode);
    // Publish last. Force overlays never cause hardware transitions.
    varstore_write(GW_VAR_REQUEST_PWR_GENERATION, generation == INT32_MAX ? 1 : generation + 1);
    return ESP_OK;
}

static esp_err_t submit(int mode, bool automatic)
{
    // Manual HTTP and the CPU1 PT share one serialized request publication.
    if (atomic_flag_test_and_set_explicit(&request_lock, memory_order_acquire))
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = request(mode, automatic);
    atomic_flag_clear_explicit(&request_lock, memory_order_release);
    return error;
}

esp_err_t gw_power_request(int mode) { return submit(mode, false); }
esp_err_t gw_power_auto_request(void) { return submit(GW_POWER_CAN_DEEP_SLEEP, true); }

static void stage(int value)
{
    deep_stage = value;
    deadline = esp_timer_get_time() + 5000000;
    varstore_write(GW_VAR_PWR_DEEP_STAGE, value);
}

static void finish(esp_err_t error)
{
    varstore_write(GW_VAR_PWR_LAST_ERROR, error);
    varstore_write(GW_VAR_PWR_COMPLETED, waiting);
    waiting = 0;
    stage(GW_DEEP_IDLE);
}

static void restore(esp_err_t error)
{
    varstore_write(GW_VAR_PWR_LAST_ERROR, error);
    varstore_write(GW_VAR_REQUEST_NETWORK_SUSPEND, 0);
    varstore_write(GW_VAR_REQUEST_CAN_SUSPEND, 0);
    stage(GW_DEEP_RESTORE);
}

static void deep_tick(void)
{
    esp_err_t error = ESP_OK;
    if (deep_stage == GW_DEEP_RESTORE) {
        // Owners restore their own peripherals. Report even a failed recovery;
        // never spin forever or enter sleep after a failed hand-off.
        if ((!producer(GW_VAR_CAN_SUSPENDED) && !producer(GW_VAR_NETWORK_SUSPENDED) &&
             !producer(GW_VAR_CAN_SUSPEND_COMPLETED) && !producer(GW_VAR_NETWORK_SUSPEND_COMPLETED)) ||
            esp_timer_get_time() >= deadline)
            finish(producer(GW_VAR_PWR_LAST_ERROR));
        return;
    }
    if (deep_stage == GW_DEEP_STOP_CAN && producer(GW_VAR_CAN_SUSPEND_COMPLETED) == 1) {
        error = producer(GW_VAR_CAN_SUSPEND_ERROR);
        if (!error && producer(GW_VAR_CAN_SUSPENDED)) {
            varstore_write(GW_VAR_REQUEST_NETWORK_SUSPEND, 1);
            stage(GW_DEEP_STOP_NETWORK);
            return;
        }
    } else if (deep_stage == GW_DEEP_STOP_NETWORK && producer(GW_VAR_NETWORK_SUSPEND_COMPLETED) == 1) {
        error = producer(GW_VAR_NETWORK_SUSPEND_ERROR);
        if (!error && producer(GW_VAR_NETWORK_SUSPENDED)) {
            stage(GW_DEEP_ENTER);
            // Successful entry does not return. No timer wake source is enabled.
            restore(gw_power_enter_can_deep_sleep());
            return;
        }
    }
    if (error || esp_timer_get_time() >= deadline)
        restore(error ? error : ESP_ERR_TIMEOUT);
}

void gw_power_policy_tick(void)
{
    if (!waiting) {
        int32_t request = producer(GW_VAR_REQUEST_PWR_GENERATION);
        if (request == producer(GW_VAR_PWR_COMPLETED)) return;
        waiting = request;
        target = producer(GW_VAR_REQUEST_PWR_MODE);
        // HTTP response leaves before changing CPU/sleep policy.
        apply_at = esp_timer_get_time() + 1500000;
        return;
    }
    // Closing the real CAN decoder invalidates its observations. Recheck age
    // until that owner acknowledges stop, then retain the admitted inactivity
    // decision while still checking settings, vehicle work and OTA.
    bool rx_stopped = deep_stage == GW_DEEP_STOP_NETWORK || deep_stage == GW_DEEP_ENTER ||
        (deep_stage == GW_DEEP_STOP_CAN && producer(GW_VAR_CAN_SUSPEND_COMPLETED) == 1);
    if (automatic_request && deep_stage != GW_DEEP_RESTORE &&
        !(rx_stopped ? gw_low_power_permitted() : gw_low_power_eligible())) {
        if (deep_stage == GW_DEEP_IDLE) finish(ESP_ERR_INVALID_STATE);
        else restore(ESP_ERR_INVALID_STATE);
        return;
    }
    if (deep_stage != GW_DEEP_IDLE) { deep_tick(); return; }
    if (esp_timer_get_time() < apply_at) return;
    if (target == GW_POWER_CAN_DEEP_SLEEP) {
        varstore_write(GW_VAR_PWR_LAST_ERROR, ESP_OK);
        varstore_write(GW_VAR_REQUEST_CAN_SUSPEND, 1);
        stage(GW_DEEP_STOP_CAN);
        return;
    }
    esp_err_t error = target >= GW_POWER_NORMAL && target <= GW_POWER_LIGHT_SLEEP ?
        apply(target) : ESP_ERR_INVALID_ARG;
    finish(error);
}
