// SPDX-License-Identifier: BSD-2-Clause
// Run the actual C component concurrently on Windows without a Windows SDK/CRT.
// The small declarations below are the Win32 thread/console ABI, not a mock of
// varstore. Clang and lld-link from the installed ESP tools are sufficient.
#include "varstore.h"
#include <limits.h>

typedef void *handle_t;
typedef unsigned long dword_t;
typedef dword_t (__stdcall *thread_fn)(void *);
#define WIN __declspec(dllimport)
WIN void __stdcall ExitProcess(unsigned int code);
WIN int __stdcall WriteFile(handle_t, const void *, dword_t, dword_t *, void *);
WIN handle_t __stdcall GetStdHandle(dword_t);
WIN handle_t __stdcall CreateThread(void *, size_t, thread_fn, void *, dword_t, dword_t *);
WIN dword_t __stdcall WaitForSingleObject(handle_t, dword_t);
WIN int __stdcall CloseHandle(handle_t);
WIN int __stdcall SwitchToThread(void);
WIN dword_t __stdcall GetCurrentThreadId(void);

void *memset(void *p, int value, size_t size) { unsigned char *b = p; while (size--) *b++ = (unsigned char)value; return p; }
void *memcpy(void *dst, const void *src, size_t size) { unsigned char *d = dst; const unsigned char *s = src; while (size--) *d++ = *s++; return dst; }
static void print(const char *s)
{
    dword_t length = 0, written;
    while (s[length]) ++length;
    WriteFile(GetStdHandle((dword_t)-11), s, length, &written, NULL);
}
#define CHECK(condition) do { if (!(condition)) { print("FAIL: " #condition "\n"); ExitProcess(1); } } while (0)
static const varstore_definition_t defs[] = {{"sample", "Test signal", "raw", 100}, {"second", "Independent signal", "raw", 0}};
static varstore_slot_t slots[2];
static uint64_t now;
static uint64_t clock_ms(void *unused) { (void)unused; return now; }
static varstore_snapshot_t inspect(uint32_t limit)
{
    return varstore_inspect(0, limit);
}

static void semantic_tests(void)
{
    varstore_init(defs, slots, 2, clock_ms, NULL);
    varstore_snapshot_t s = inspect(100);
    CHECK(s.state == VARSTORE_INVALID && !s.has_value && !s.raw_has_value);
    CHECK(varstore_read(0, 100, -999) == -999);
    CHECK(!varstore_check_valid(0, 0));
    varstore_touch(0);
    now = 1000;
    varstore_write(0, INT32_MIN);
    CHECK(varstore_read(0, 100, 7) == INT32_MIN);
    now = 1100;
    CHECK(varstore_check_valid(0, 100)); // Inclusive max_age boundary.
    now = 1101;
    CHECK(!varstore_check_valid(0, 100));
    CHECK(varstore_read(0, 100, 7) == 7);
    CHECK(varstore_read(0, 0, 7) == INT32_MIN); // Any age.
    CHECK(varstore_read(0, 200, 7) == INT32_MIN); // Caller controls freshness.
    varstore_touch(0);
    CHECK(inspect(100).age_ms == 0 && varstore_check_valid(0, 100));
    varstore_invalidate(0);
    CHECK(varstore_read(0, 0, 9) == 9);
    varstore_touch(0);
    varstore_force(0, true, INT32_MAX);
    now += 100000;
    CHECK(varstore_read(0, 1, 0) == INT32_MAX);
    CHECK(varstore_check_valid(0, 1));
    varstore_write(0, -42);
    s = inspect(100);
    CHECK(s.state == VARSTORE_FORCED && s.value == INT32_MAX && s.raw_value == -42 && s.raw_valid);
    varstore_force(0, false, 123);
    CHECK(varstore_read(0, 100, 0) == -42);
    varstore_force(0, true, 23);
    now += 101;
    varstore_force(0, false, 0);
    CHECK(varstore_read(0, 100, -8) == -8); // Release exposes actual old age.
    varstore_init(defs, slots, 2, clock_ms, NULL);
    varstore_force(0, true, -17);
    CHECK(varstore_check_valid(0, 1));
    CHECK(!inspect(1).raw_has_value);
    varstore_force(0, false, 0);
    CHECK(!varstore_check_valid(0, 0));
    CHECK(varstore_read(8, 0, -77) == -77);
    varstore_write(8, 3);
    varstore_init(NULL, slots, 2, clock_ms, NULL);
    CHECK(varstore_count() == 0 && varstore_read(0, 0, -7) == -7);
    CHECK(varstore_inspect(8, 0).state == VARSTORE_INVALID);
    varstore_init(defs, slots, 2, clock_ms, NULL);
    varstore_touch(0);
    CHECK(!varstore_check_valid(0, 0));

    // Cross the 32-bit millisecond rollover, then verify saturated age.
    now = (uint64_t)UINT32_MAX - 4;
    varstore_write(0, 123);
    now += 10;
    CHECK(inspect(100).age_ms == 10 && varstore_read(0, 100, 0) == 123);
    now += (uint64_t)UINT32_MAX + 20;
    CHECK(inspect(100).age_ms == UINT32_MAX && !varstore_check_valid(0, 100));

    // Stable overrides remain readable independently of raw publication.
    __atomic_store_n(&slots[0].sequence, 1, __ATOMIC_SEQ_CST);
    varstore_force(0, true, 81);
    CHECK(varstore_read(0, 1, -66) == 81 && varstore_check_valid(0, 1));
    varstore_write(1, 2);
    CHECK(varstore_read(1, 0, -1) == 2);
    __atomic_store_n(&slots[0].sequence, UINT32_MAX - 1, __ATOMIC_SEQ_CST);
    varstore_write(0, 12); // Sequence wraps to zero.
    CHECK(__atomic_load_n(&slots[0].sequence, __ATOMIC_SEQ_CST) == 0);
    varstore_force(0, false, 0);
    CHECK(varstore_read(0, 0, 0) == 12);
    __atomic_store_n(&slots[0].force_sequence, UINT32_MAX - 1, __ATOMIC_SEQ_CST);
    varstore_force(0, true, 99);
    CHECK(__atomic_load_n(&slots[0].force_sequence, __ATOMIC_SEQ_CST) == 0);
    CHECK(varstore_read(0, 0, -66) == 99);
    print("PASS: initialization, fallback, expiry, touch, force/release and counter/time rollover\n");
}

enum { ITERATIONS = 150000, READER_ITERATIONS = 100000 };
static uint32_t waiting;
static dword_t __stdcall waiting_reader(void *unused)
{
    (void)unused;
    __atomic_store_n(&waiting, 1, __ATOMIC_SEQ_CST);
    CHECK(varstore_read(0, 0, -66) == 12);
    return 0;
}
static void retry_test(void)
{
    varstore_init(defs, slots, 2, clock_ms, NULL);
    varstore_write(0, 12);
    // Artificially pause a publication: a reader must wait, not use fallback.
    __atomic_store_n(&slots[0].sequence, 3, __ATOMIC_SEQ_CST);
    handle_t thread = CreateThread(NULL, 0, waiting_reader, NULL, 0, NULL);
    CHECK(thread != NULL);
    while (!__atomic_load_n(&waiting, __ATOMIC_SEQ_CST)) SwitchToThread();
    CHECK(WaitForSingleObject(thread, 50) == 258); // WAIT_TIMEOUT
    __atomic_store_n(&slots[0].sequence, 4, __ATOMIC_SEQ_CST);
    CHECK(WaitForSingleObject(thread, 60000) == 0);
    CloseHandle(thread);
    print("PASS: reader retries a paused publication instead of returning fallback\n");
}
static uint32_t go, producer_id, producer_tick, successes;
static const uint64_t epoch = (uint64_t)UINT32_MAX - 70000;
static uint64_t stress_clock(void *unused)
{
    (void)unused;
    if (GetCurrentThreadId() == __atomic_load_n(&producer_id, __ATOMIC_SEQ_CST))
        return epoch + __atomic_load_n(&producer_tick, __ATOMIC_SEQ_CST);
    return epoch + ITERATIONS + 100;
}
static void await_start(void) { while (!__atomic_load_n(&go, __ATOMIC_SEQ_CST)) SwitchToThread(); }
static dword_t __stdcall producer(void *unused)
{
    (void)unused;
    __atomic_store_n(&producer_id, (uint32_t)GetCurrentThreadId(), __ATOMIC_SEQ_CST);
    await_start();
    for (int32_t n = 1; n <= ITERATIONS; ++n) {
        __atomic_store_n(&producer_tick, (uint32_t)n, __ATOMIC_SEQ_CST);
        varstore_write(0, n);
        if (!(n & 255)) SwitchToThread();
    }
    return 0;
}
static dword_t __stdcall controller(void *unused)
{
    (void)unused; await_start();
    for (int32_t n = 1; n <= ITERATIONS; ++n) {
        varstore_force(0, true, -n);
        if (!(n & 1)) varstore_force(0, false, 0);
        if (!(n & 255)) SwitchToThread();
    }
    return 0;
}
static dword_t __stdcall reader(void *unused)
{
    (void)unused; await_start();
    for (unsigned n = 0; n < READER_ITERATIONS; ++n) {
        varstore_snapshot_t sample = varstore_inspect(0, ITERATIONS + 100);
        if (sample.raw_has_value) {
            CHECK(sample.raw_value > 0 && sample.raw_value <= ITERATIONS);
            // Value and both timestamp halves MUST belong to the same update.
            CHECK((uint32_t)sample.raw_value + sample.raw_age_ms == ITERATIONS + 100);
            CHECK(sample.raw_valid);
        }
        if (sample.state == VARSTORE_FORCED) CHECK(sample.value < 0 && sample.value >= -ITERATIONS);
        else if (sample.state == VARSTORE_VALID) CHECK(sample.value == sample.raw_value);
        __atomic_fetch_add(&successes, 1, __ATOMIC_SEQ_CST);
    }
    return 0;
}
static void concurrent_tests(void)
{
    varstore_init(defs, slots, 2, stress_clock, NULL);
    handle_t threads[4] = {CreateThread(NULL, 0, producer, NULL, 0, NULL),
                           CreateThread(NULL, 0, controller, NULL, 0, NULL),
                           CreateThread(NULL, 0, reader, NULL, 0, NULL),
                           CreateThread(NULL, 0, reader, NULL, 0, NULL)};
    for (unsigned i = 0; i < 4; ++i) CHECK(threads[i] != NULL);
    __atomic_store_n(&go, 1, __ATOMIC_SEQ_CST);
    for (unsigned i = 0; i < 4; ++i) { CHECK(WaitForSingleObject(threads[i], 60000) == 0); CloseHandle(threads[i]); }
    CHECK(__atomic_load_n(&successes, __ATOMIC_SEQ_CST) == 2 * READER_ITERATIONS);
    varstore_force(0, false, 0);
    CHECK(varstore_read(0, ITERATIONS + 100, 0) == ITERATIONS);
    print("PASS: 150,000 producer updates + 225,000 override updates + two concurrent snapshot readers\n");
}
void mainCRTStartup(void)
{
    semantic_tests(); retry_test(); concurrent_tests(); print("All varstore tests passed.\n"); ExitProcess(0);
}
