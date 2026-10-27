// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    VARSTORE_INVALID = 0,
    VARSTORE_VALID,
    VARSTORE_FORCED,
} varstore_state_t;

// Static-lifetime metadata; const strings can stay in memory-mapped flash.
typedef struct {
    const char *name;
    const char *description;
    const char *unit;
    uint32_t max_age_ms; // Default freshness limit for diagnostics; zero = any age.
} varstore_definition_t;

// Private storage, exposed only to permit static allocation from C or C++.
// Do not inspect/change fields directly. On ESP32-S3 allocate in internal RAM.
typedef struct {
    uint32_t sequence, value, stamp_low, stamp_high, flags;
    uint32_t force_sequence, force_value, force_stamp_low, force_stamp_high, force_enabled;
} varstore_slot_t;

typedef uint64_t (*varstore_clock_fn)(void *context); // Monotonic milliseconds.


typedef struct {
    int32_t value;
    uint32_t age_ms; // Saturates at UINT32_MAX. Undefined when !has_value.
    varstore_state_t state;
    bool has_value;
    int32_t raw_value;
    uint32_t raw_age_ms;
    bool raw_has_value;
    bool raw_valid;
} varstore_snapshot_t;

// Initialize once, before concurrent access. No allocations, tasks or timers.
void varstore_init(const varstore_definition_t *definitions,
                   varstore_slot_t *slots, size_t count,
                   varstore_clock_fn clock, void *clock_context);
size_t varstore_count(void);
const varstore_definition_t *varstore_definition(size_t id);

// Consumer API: forced values are always valid, irrespective of their age.
// max_age_ms == 0 disables the age check; otherwise age <= max_age_ms is valid.
// Invalid or unknown signals return fallback. A concurrent update is retried
// until a consistent snapshot is obtained; readers do not return BUSY.
int32_t varstore_read(size_t id, uint32_t max_age_ms, int32_t fallback);
bool varstore_check_valid(size_t id, uint32_t max_age_ms);

// Diagnostic API, including real producer data beneath an override.
// Single raw writer and single force controller per signal; task context only.
varstore_snapshot_t varstore_inspect(size_t id, uint32_t max_age_ms);
void varstore_write(size_t id, int32_t value);
void varstore_force(size_t id, bool enabled, int32_t value);
// Refresh/revalidate an existing sample; a never-written signal stays invalid.
void varstore_touch(size_t id);
void varstore_invalidate(size_t id);
const char *varstore_state_name(varstore_state_t state);

#ifdef __cplusplus
}
#endif
