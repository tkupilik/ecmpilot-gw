// SPDX-License-Identifier: BSD-2-Clause
// Fresh implementation: no code imported from opel_dash or UFRM.
#include "varstore.h"
#include <limits.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#endif

_Static_assert(sizeof(uint32_t) == 4, "32-bit words required");
_Static_assert(__atomic_always_lock_free(4, 0), "Native lock-free 32-bit atomics required");
_Static_assert(_Alignof(varstore_slot_t) >= 4, "Atomic words must be aligned");
enum { HAS_VALUE = 1, SAMPLE_VALID = 2 };

typedef struct {
    const varstore_definition_t *definitions;
    varstore_slot_t *slots;
    size_t count;
    varstore_clock_fn clock;
    void *clock_context;
} varstore_context_t;
static varstore_context_t context;
static varstore_context_t *store;

// Payload words are atomic too: no C data races, even during a rejected read.
// Sequential consistency keeps payload operations between sequence checks.
static uint32_t load(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void save(uint32_t *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

// Prevent a higher-priority reader on this core from preempting an odd writer.
// This is local interrupt masking, not a mutex or cross-core spinlock. The
// protected window contains only native atomic accesses; fetch time beforehand.
static uint32_t writer_enter(void)
{
#ifdef ESP_PLATFORM
    return portSET_INTERRUPT_MASK_FROM_ISR();
#else
    return 0;
#endif
}
static void writer_leave(uint32_t level)
{
#ifdef ESP_PLATFORM
    portCLEAR_INTERRUPT_MASK_FROM_ISR(level);
#else
    (void)level;
#endif
}
static void save_time(uint32_t *lo, uint32_t *hi, uint64_t now)
{
    save(lo, (uint32_t)now);
    save(hi, (uint32_t)(now >> 32));
}
static uint64_t load_time(const uint32_t *lo, const uint32_t *hi)
{
    const uint32_t lower = load(lo);
    return ((uint64_t)load(hi) << 32) | lower;
}
static uint64_t elapsed(uint64_t now, uint64_t then) { return now >= then ? now - then : UINT64_MAX; }
static uint32_t age(uint64_t ms) { return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms; }
static int32_t signed_value(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}
static bool exists(size_t id) { return store && id < store->count; }

void varstore_init(const varstore_definition_t *defs, varstore_slot_t *slots,
                   size_t count, varstore_clock_fn clock, void *clock_context)
{
    store = NULL;
    if (!defs || !slots || !count || !clock) return;
    for (size_t i = 0; i < count; ++i)
        if (!defs[i].name || !defs[i].name[0] || !defs[i].description || !defs[i].unit) return;
    for (size_t i = 0; i < count; ++i) {
        varstore_slot_t *v = &slots[i];
        save(&v->sequence, 0); save(&v->value, 0); save(&v->stamp_low, 0);
        save(&v->stamp_high, 0); save(&v->flags, 0);
        save(&v->force_sequence, 0); save(&v->force_value, 0);
        save(&v->force_stamp_low, 0); save(&v->force_stamp_high, 0); save(&v->force_enabled, 0);
    }
    context = (varstore_context_t){defs, slots, count, clock, clock_context};
    store = &context;
}
size_t varstore_count(void) { return store ? store->count : 0; }
const varstore_definition_t *varstore_definition(size_t id)
{
    return exists(id) ? &store->definitions[id] : NULL;
}

varstore_snapshot_t varstore_inspect(size_t id, uint32_t max_age_ms)
{
    if (!exists(id)) return (varstore_snapshot_t){.age_ms = UINT32_MAX, .raw_age_ms = UINT32_MAX};
    const varstore_slot_t *v = &store->slots[id];
    for (;;) {
        const uint32_t seq = load(&v->sequence), force_seq = load(&v->force_sequence);
        if ((seq | force_seq) & 1) continue;
        const uint32_t value = load(&v->value), flags = load(&v->flags);
        const uint64_t stamp = load_time(&v->stamp_low, &v->stamp_high);
        const uint32_t forced = load(&v->force_enabled), force_value = load(&v->force_value);
        const uint64_t force_stamp = load_time(&v->force_stamp_low, &v->force_stamp_high);
        if (seq != load(&v->sequence) || force_seq != load(&v->force_sequence)) continue;
        const uint64_t now = store->clock(store->clock_context), raw_age = elapsed(now, stamp);
        const bool has_value = (flags & HAS_VALUE) != 0;
        const bool valid = has_value && (flags & SAMPLE_VALID) && (!max_age_ms || raw_age <= max_age_ms);
        return (varstore_snapshot_t){
            .value = signed_value(forced ? force_value : value),
            .age_ms = forced ? age(elapsed(now, force_stamp)) : (has_value ? age(raw_age) : UINT32_MAX),
            .state = forced ? VARSTORE_FORCED : (valid ? VARSTORE_VALID : VARSTORE_INVALID),
            .has_value = forced || has_value,
            .raw_value = signed_value(value),
            .raw_age_ms = has_value ? age(raw_age) : UINT32_MAX,
            .raw_has_value = has_value,
            .raw_valid = valid,
        };
    }
}

// A stable override does not depend on the producer's independent sequence.
static varstore_snapshot_t inspect_effective(size_t id, uint32_t max_age_ms)
{
    if (!exists(id)) return (varstore_snapshot_t){.state = VARSTORE_INVALID};
    const varstore_slot_t *v = &store->slots[id];
    for (;;) {
        const uint32_t seq = load(&v->force_sequence);
        if (seq & 1) continue;
        const uint32_t enabled = load(&v->force_enabled), value = load(&v->force_value);
        if (seq != load(&v->force_sequence)) continue;
        if (enabled) return (varstore_snapshot_t){.value = signed_value(value), .state = VARSTORE_FORCED};
        return varstore_inspect(id, max_age_ms);
    }
}
int32_t varstore_read(size_t id, uint32_t max_age_ms, int32_t fallback)
{
    const varstore_snapshot_t sample = inspect_effective(id, max_age_ms);
    return sample.state != VARSTORE_INVALID ? sample.value : fallback;
}
bool varstore_check_valid(size_t id, uint32_t max_age_ms)
{
    return inspect_effective(id, max_age_ms).state != VARSTORE_INVALID;
}

void varstore_write(size_t id, int32_t value)
{
    if (!exists(id)) return;
    varstore_write_at(id, value, store->clock(store->clock_context));
}
void varstore_write_at(size_t id, int32_t value, uint64_t timestamp_ms)
{
    if (!exists(id)) return;
    varstore_slot_t *v = &store->slots[id];
    const uint64_t now = timestamp_ms;
    const uint32_t level = writer_enter(), seq = load(&v->sequence);
    save(&v->sequence, seq + 1);
    save(&v->value, (uint32_t)value);
    save_time(&v->stamp_low, &v->stamp_high, now);
    save(&v->flags, HAS_VALUE | SAMPLE_VALID);
    save(&v->sequence, seq + 2);
    writer_leave(level);
}
void varstore_force(size_t id, bool enabled, int32_t value)
{
    if (!exists(id)) return;
    varstore_slot_t *v = &store->slots[id];
    const uint64_t now = enabled ? store->clock(store->clock_context) : 0;
    const uint32_t level = writer_enter(), seq = load(&v->force_sequence);
    save(&v->force_sequence, seq + 1);
    if (enabled) {
        save(&v->force_value, (uint32_t)value);
        save_time(&v->force_stamp_low, &v->force_stamp_high, now);
    }
    save(&v->force_enabled, enabled ? 1 : 0);
    save(&v->force_sequence, seq + 2);
    writer_leave(level);
}
void varstore_touch(size_t id)
{
    if (!exists(id)) return;
    varstore_slot_t *v = &store->slots[id];
    if (!(load(&v->flags) & HAS_VALUE)) return;
    const uint64_t now = store->clock(store->clock_context);
    const uint32_t level = writer_enter(), seq = load(&v->sequence);
    save(&v->sequence, seq + 1);
    save_time(&v->stamp_low, &v->stamp_high, now);
    save(&v->flags, HAS_VALUE | SAMPLE_VALID);
    save(&v->sequence, seq + 2);
    writer_leave(level);
}
void varstore_invalidate(size_t id)
{
    if (!exists(id)) return;
    varstore_slot_t *v = &store->slots[id];
    const uint32_t level = writer_enter(), seq = load(&v->sequence);
    save(&v->sequence, seq + 1);
    save(&v->flags, load(&v->flags) & ~SAMPLE_VALID);
    save(&v->sequence, seq + 2);
    writer_leave(level);
}
const char *varstore_state_name(varstore_state_t state)
{
    switch (state) {
    case VARSTORE_VALID: return "valid";
    case VARSTORE_FORCED: return "forced";
    default: return "invalid";
    }
}
