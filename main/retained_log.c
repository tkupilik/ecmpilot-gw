// SPDX-License-Identifier: BSD-2-Clause
#include "retained_log.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define RTC_NOINIT_ATTR
#endif

// Two banks prevent startup logging from overwriting the previous boot's tail.
// No heap, filesystem or flash writes. Disabled RTC initialization is essential.
enum { MAGIC = 0x444C4701U };
typedef struct {
    uint32_t sequence;
    uint32_t checksum;
    char text[GW_LOG_TEXT_SIZE];
} RetainedEntry;
typedef struct {
    uint32_t magic;
    uint32_t boot;
    uint32_t boot_inverse;
    RetainedEntry entries[GW_RETAINED_LOG_CAPACITY];
} RetainedBank;
static RTC_NOINIT_ATTR RetainedBank banks[2];
_Static_assert(sizeof(banks) <= 7168, "Leave room for other RTC data");
static int current = -1, previous = -1;
static uint32_t next = 1;

static bool bank_valid(const RetainedBank *bank)
{
    return __atomic_load_n(&bank->magic, __ATOMIC_ACQUIRE) == MAGIC &&
        bank->boot != 0 && bank->boot_inverse == ~bank->boot;
}

static uint32_t checksum(const RetainedBank *bank, const RetainedEntry *entry, uint32_t sequence)
{
    uint32_t sum = 2166136261U ^ bank->boot ^ sequence;
    for (size_t i = 0; i < sizeof(entry->text); ++i)
        sum = (sum ^ (unsigned char)entry->text[i]) * 16777619U;
    return sum;
}

void gw_retained_log_boot(bool preserve)
{
    previous = -1;
    if (preserve) {
        if (bank_valid(&banks[0])) previous = 0;
        if (bank_valid(&banks[1]) && (previous < 0 ||
            (int32_t)(banks[1].boot - banks[0].boot) > 0)) previous = 1;
    } else {
        // A cold start must not expose unrelated retained memory as a boot log.
        __atomic_store_n(&banks[0].magic, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&banks[1].magic, 0, __ATOMIC_RELEASE);
    }
    uint32_t boot = previous < 0 ? 1 : banks[previous].boot + 1;
    if (!boot) boot = 1;
    current = previous == 0 ? 1 : 0;
    RetainedBank *bank = &banks[current];
    __atomic_store_n(&bank->magic, 0, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memset(bank->entries, 0, sizeof(bank->entries));
    bank->boot = boot;
    bank->boot_inverse = ~boot;
    __atomic_store_n(&bank->magic, MAGIC, __ATOMIC_RELEASE);
    next = 1;
}

void gw_retained_log_append(const char *text)
{
    if (current < 0 || !text || !text[0]) return;
    RetainedBank *bank = &banks[current];
    RetainedEntry *entry = &bank->entries[(next - 1) % GW_RETAINED_LOG_CAPACITY];
    // Reset during publication loses at most this slot. Sequence is the commit
    // word; checksum rejects partially written/corrupted records after reset.
    __atomic_store_n(&entry->sequence, 0, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    size_t length = strnlen(text, sizeof(entry->text) - 1);
    memcpy(entry->text, text, length);
    memset(entry->text + length, 0, sizeof(entry->text) - length);
    entry->checksum = checksum(bank, entry, next);
    __atomic_store_n(&entry->sequence, next, __ATOMIC_RELEASE);
    // About 4 billion log records per boot; reset the ring before wrapping.
    if (++next == 0) {
        memset(bank->entries, 0, sizeof(bank->entries));
        next = 1;
    }
}

static bool entry_valid(const RetainedBank *bank, const RetainedEntry *entry)
{
    uint32_t sequence = __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
    return sequence != 0 && entry->text[sizeof(entry->text) - 1] == 0 &&
        entry->checksum == checksum(bank, entry, sequence);
}

gw_log_page_t gw_retained_log_read(uint64_t after, gw_log_entry_t *entries, size_t capacity)
{
    gw_log_page_t page = {0};
    if (previous < 0 || !entries || !capacity) return page;
    if (capacity > GW_LOG_PAGE_SIZE) capacity = GW_LOG_PAGE_SIZE;
    const RetainedBank *bank = &banks[previous];
    for (unsigned i = 0; i < GW_RETAINED_LOG_CAPACITY; ++i) {
        const RetainedEntry *entry = &bank->entries[i];
        if (!entry_valid(bank, entry)) continue;
        if (!page.oldest || entry->sequence < page.oldest) page.oldest = entry->sequence;
        if (entry->sequence > page.latest) page.latest = entry->sequence;
    }
    if (after > page.latest) after = 0;
    uint64_t first = after + 1;
    if (page.oldest && first < page.oldest) { page.lost = page.oldest - first; first = page.oldest; }
    page.next = after;
    for (; first <= page.latest && page.count < capacity; ++first) {
        const RetainedEntry *entry = &bank->entries[(first - 1) % GW_RETAINED_LOG_CAPACITY];
        page.next = first;
        if (entry->sequence != first || !entry_valid(bank, entry)) continue;
        entries[page.count].sequence = first;
        memcpy(entries[page.count++].text, entry->text, sizeof(entry->text));
    }
    return page;
}

unsigned gw_retained_log_count(void)
{
    unsigned count = 0;
    if (previous >= 0)
        for (unsigned i = 0; i < GW_RETAINED_LOG_CAPACITY; ++i)
            count += entry_valid(&banks[previous], &banks[previous].entries[i]);
    return count;
}

unsigned gw_retained_log_boot_number(void)
{
    return current < 0 ? 0 : banks[current].boot;
}
