#include "runtime_signal_store.h"

#include <string.h>

#include "esp_timer.h"

#define RUNTIME_SIGNAL_INVALID_AGE_MS UINT32_MAX

static bool signal_id_ok(const RuntimeSignalStore *store, size_t id)
{
    return store != NULL && store->signals != NULL && id < store->count;
}

static bool signal_valid_locked(const RuntimeSignal *signal, int64_t now_us)
{
    if ((signal->flags & RUNTIME_SIGNAL_FORCED) != 0) {
        return true;
    }

    if ((signal->flags & RUNTIME_SIGNAL_VALID) == 0) {
        return false;
    }

    if (signal->max_age_ms == 0) {
        return true;
    }

    int64_t age_us = now_us - signal->updated_us;
    return age_us >= 0 && age_us <= ((int64_t)signal->max_age_ms * 1000);
}

esp_err_t runtime_signal_store_init(RuntimeSignalStore *store,
                                    RuntimeSignal *signals,
                                    size_t count,
                                    const RuntimeSignalDesc *descs)
{
    if (store == NULL || signals == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(signals, 0, sizeof(signals[0]) * count);

    store->signals = signals;
    store->descs = descs;
    store->count = count;
    store->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    for (size_t i = 0; i < count; ++i) {
        signals[i].updated_us = 0;
        signals[i].max_age_ms = descs != NULL ? descs[i].max_age_ms : 0;
    }

    return ESP_OK;
}

esp_err_t runtime_signal_write(RuntimeSignalStore *store, size_t id, int32_t value)
{
    if (!signal_id_ok(store, id)) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&store->lock);
    RuntimeSignal *signal = &store->signals[id];
    signal->value = value;
    signal->updated_us = now_us;
    signal->flags |= RUNTIME_SIGNAL_VALID;
    signal->flags &= (uint8_t)~RUNTIME_SIGNAL_ERROR;
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}

esp_err_t runtime_signal_read(RuntimeSignalStore *store, size_t id, int32_t *value)
{
    if (!signal_id_ok(store, id) || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t res = ESP_ERR_INVALID_STATE;
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&store->lock);
    RuntimeSignal *signal = &store->signals[id];
    if (signal_valid_locked(signal, now_us)) {
        *value = (signal->flags & RUNTIME_SIGNAL_FORCED) != 0 ? signal->forced_value : signal->value;
        res = ESP_OK;
    }
    portEXIT_CRITICAL(&store->lock);

    return res;
}

esp_err_t runtime_signal_peek(RuntimeSignalStore *store, size_t id, int32_t *value)
{
    if (!signal_id_ok(store, id) || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&store->lock);
    RuntimeSignal *signal = &store->signals[id];
    *value = (signal->flags & RUNTIME_SIGNAL_FORCED) != 0 ? signal->forced_value : signal->value;
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}

bool runtime_signal_is_valid(RuntimeSignalStore *store, size_t id)
{
    if (!signal_id_ok(store, id)) {
        return false;
    }

    bool valid;
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&store->lock);
    valid = signal_valid_locked(&store->signals[id], now_us);
    portEXIT_CRITICAL(&store->lock);

    return valid;
}

uint32_t runtime_signal_age_ms(RuntimeSignalStore *store, size_t id)
{
    if (!signal_id_ok(store, id)) {
        return RUNTIME_SIGNAL_INVALID_AGE_MS;
    }

    uint32_t age_ms = RUNTIME_SIGNAL_INVALID_AGE_MS;
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&store->lock);
    RuntimeSignal *signal = &store->signals[id];
    if ((signal->flags & RUNTIME_SIGNAL_VALID) != 0 || (signal->flags & RUNTIME_SIGNAL_FORCED) != 0) {
        int64_t age_us = now_us - signal->updated_us;
        if (age_us >= 0) {
            age_ms = (uint32_t)(age_us / 1000);
        }
    }
    portEXIT_CRITICAL(&store->lock);

    return age_ms;
}

uint8_t runtime_signal_flags(RuntimeSignalStore *store, size_t id)
{
    if (!signal_id_ok(store, id)) {
        return 0;
    }

    uint8_t flags;

    portENTER_CRITICAL(&store->lock);
    flags = store->signals[id].flags;
    portEXIT_CRITICAL(&store->lock);

    return flags;
}

const char *runtime_signal_name(const RuntimeSignalStore *store, size_t id)
{
    if (store == NULL || store->descs == NULL || id >= store->count) {
        return NULL;
    }

    return store->descs[id].name;
}

esp_err_t runtime_signal_force(RuntimeSignalStore *store, size_t id, int32_t value)
{
    if (!signal_id_ok(store, id)) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&store->lock);
    RuntimeSignal *signal = &store->signals[id];
    signal->forced_value = value;
    signal->updated_us = now_us;
    signal->flags |= RUNTIME_SIGNAL_FORCED | RUNTIME_SIGNAL_VALID;
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}

esp_err_t runtime_signal_unforce(RuntimeSignalStore *store, size_t id)
{
    if (!signal_id_ok(store, id)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&store->lock);
    store->signals[id].flags &= (uint8_t)~RUNTIME_SIGNAL_FORCED;
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}

esp_err_t runtime_signal_invalidate(RuntimeSignalStore *store, size_t id)
{
    if (!signal_id_ok(store, id)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&store->lock);
    store->signals[id].flags &= (uint8_t)~RUNTIME_SIGNAL_VALID;
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}

esp_err_t runtime_signal_set_error(RuntimeSignalStore *store, size_t id, bool error)
{
    if (!signal_id_ok(store, id)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&store->lock);
    if (error) {
        store->signals[id].flags |= RUNTIME_SIGNAL_ERROR;
    } else {
        store->signals[id].flags &= (uint8_t)~RUNTIME_SIGNAL_ERROR;
    }
    portEXIT_CRITICAL(&store->lock);

    return ESP_OK;
}
