#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RUNTIME_SIGNAL_VALID = 1u << 0,
    RUNTIME_SIGNAL_FORCED = 1u << 1,
    RUNTIME_SIGNAL_ERROR = 1u << 2,
} RuntimeSignalFlags;

typedef struct {
    const char *name;
    uint32_t max_age_ms;
} RuntimeSignalDesc;

typedef struct {
    int32_t value;
    int32_t forced_value;
    int64_t updated_us;
    uint32_t max_age_ms;
    uint8_t flags;
} RuntimeSignal;

typedef struct {
    RuntimeSignal *signals;
    const RuntimeSignalDesc *descs;
    size_t count;
    portMUX_TYPE lock;
} RuntimeSignalStore;

esp_err_t runtime_signal_store_init(RuntimeSignalStore *store,
                                    RuntimeSignal *signals,
                                    size_t count,
                                    const RuntimeSignalDesc *descs);

esp_err_t runtime_signal_write(RuntimeSignalStore *store, size_t id, int32_t value);
esp_err_t runtime_signal_read(RuntimeSignalStore *store, size_t id, int32_t *value);
esp_err_t runtime_signal_peek(RuntimeSignalStore *store, size_t id, int32_t *value);

bool runtime_signal_is_valid(RuntimeSignalStore *store, size_t id);
uint32_t runtime_signal_age_ms(RuntimeSignalStore *store, size_t id);
uint8_t runtime_signal_flags(RuntimeSignalStore *store, size_t id);
const char *runtime_signal_name(const RuntimeSignalStore *store, size_t id);

esp_err_t runtime_signal_force(RuntimeSignalStore *store, size_t id, int32_t value);
esp_err_t runtime_signal_unforce(RuntimeSignalStore *store, size_t id);
esp_err_t runtime_signal_invalidate(RuntimeSignalStore *store, size_t id);
esp_err_t runtime_signal_set_error(RuntimeSignalStore *store, size_t id, bool error);

#ifdef __cplusplus
}
#endif
