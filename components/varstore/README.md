# Varstore

Independent BSD-2-Clause implementation for global int32 process signals. No code was copied from `opel_dash` or UFRM. Registration and slots are statically allocated; the component creates no tasks, timers or heap allocations.

## API

Register one definition array, slot array and monotonic millisecond clock with `varstore_init()` before starting concurrent users. Definitions and strings must remain alive; const metadata can stay in memory-mapped flash. Slots must be in aligned internal RAM on ESP32-S3. Invalid registration leaves the store empty; use `varstore_count()` to check startup registration.

```c
static const varstore_definition_t definitions[] = {
    {"outside.temperature", "Outside temperature, tenths of a degree", "0.1 C", 5000},
};
static varstore_slot_t slots[1]; // Internal RAM; use DRAM_ATTR when appropriate.
// uint64_t clock_ms(void *context): provide a monotonic clock.
varstore_init(definitions, slots, 1, clock_ms, NULL);

varstore_write(0, 105);
int32_t temperature = varstore_read(0, 5000, -999);
bool valid = varstore_check_valid(0, 5000);
varstore_force(0, true, -50);
varstore_force(0, false, 0);
varstore_touch(0);
varstore_invalidate(0);
varstore_snapshot_t diagnostic = varstore_inspect(0, 5000);
```

All signals start invalid, without a sample. Write stores the real value, marks it valid and refreshes its timestamp. Touch refreshes/revalidates an existing real sample, including an expired or explicitly invalidated one; it does nothing to a never-written signal. Invalidate preserves the value and timestamp for diagnostics but marks the real sample invalid.

Read returns fallback only for an unknown, invalid or expired signal. `max_age_ms == 0` disables expiry; otherwise age equal to the limit is still valid. The caller controls freshness. The definition's age limit is a default for diagnostic clients, not a second implicit limit on reads.

Force has separate storage and sequence state. A forced value is always valid, even before the producer's first write and regardless of age. Producers continue updating real data while forced. Release immediately exposes the real sample and its actual validity/age; it does not refresh that sample. Repeating Force changes its value and timestamp. Inspect returns both effective and real producer data, including whether a sample has ever existed. Forced age is time since Force, for display only. Reboot clears all samples and overrides.

Mutation functions return void. Unknown IDs are ignored by mutations, return fallback/false to consumers and produce an invalid diagnostic snapshot. Definition lookup returns NULL for an unknown ID. HTTP callers validate IDs before calling this API.

## Concurrency contract

- Exactly **one producer task per signal** calls Write/Touch/Invalidate. Different signals may have different producers on either core.
- Exactly **one force-controller task per signal** calls Force/Release. It can run concurrently with the producer and any number of readers.
- API calls are supported from **task context**, not interrupt handlers. Initialize only before concurrent access. Do not mutate slot fields directly.
- Writers must publish finite, short updates and allow readers to progress; a task spinning indefinitely through writes violates the intended application contract.

Every shared payload and sequence word uses a native lock-free 32-bit atomic operation. Even sequences describe complete publications; odd sequences describe updates in progress. A reader accepts a snapshot only if both independently sampled sequence words remain unchanged and even. It retries indefinitely until consistent; there is no BUSY result or retry-budget fallback. Consumers can read a stable forced value independently of a raw update.

On ESP32-S3, the tiny odd-sequence publication window masks interrupts **on the writer's local core** and restores the previous mask immediately afterwards. This prevents a higher-priority reader on that core from preempting its writer and spinning forever. There is no semaphore, shared spinlock or cross-core critical-section lock. The timestamp callback runs before masking; only native atomic operations execute inside the window. Keep that distinction if extending this component.

Timestamps are 64-bit monotonic milliseconds, published as two atomic 32-bit halves under the sequence. This avoids emulated 64-bit atomics and the 49-day wrap of a 32-bit timestamp. Reported ages saturate at UINT32_MAX; freshness checks use the full elapsed time. Sequence arithmetic wraps naturally. As with sequence-counter schemes generally, a reader must not span 2^31 publications to the same domain, which could make the counter appear unchanged.

The Windows host tests exercise the same C source with real threads. Local interrupt masking is ESP-specific and must additionally be checked on the device; see the project testing guide.
