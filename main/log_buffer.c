// SPDX-License-Identifier: BSD-2-Clause
#include "log_buffer.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static void enter(void) { portENTER_CRITICAL(&lock); }
static void leave(void) { portEXIT_CRITICAL(&lock); }
#else
// Host tests use real concurrent threads. ESP uses the FreeRTOS cross-core
// critical section above; this lock is unrelated to the varstore component.
static unsigned lock;
static void enter(void) { while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) {} }
static void leave(void) { __atomic_store_n(&lock, 0, __ATOMIC_RELEASE); }
#endif
static gw_log_entry_t ring[GW_LOG_CAPACITY];
static uint64_t next_sequence = 1;

void gw_log_buffer_reset(void)
{
    enter();
    next_sequence = 1;
    memset(ring, 0, sizeof(ring));
    leave();
}
void gw_log_buffer_append(const char *text)
{
    if (!text || !text[0]) return;
    const size_t length = strnlen(text, GW_LOG_TEXT_SIZE - 1);
    enter();
    gw_log_entry_t *entry = &ring[(next_sequence - 1) % GW_LOG_CAPACITY];
    entry->sequence = next_sequence++;
    memcpy(entry->text, text, length);
    entry->text[length] = 0;
    leave();
}
gw_log_page_t gw_log_buffer_read(uint64_t after, gw_log_entry_t *entries, size_t capacity)
{
    gw_log_page_t page = {0};
    if (!entries || !capacity) return page;
    if (capacity > GW_LOG_PAGE_SIZE) capacity = GW_LOG_PAGE_SIZE;
    enter();
    page.latest = next_sequence - 1;
    page.oldest = page.latest ? (page.latest < GW_LOG_CAPACITY ? 1 : page.latest - GW_LOG_CAPACITY + 1) : 0;
    if (after > page.latest) after = 0;
    uint64_t first = after + 1;
    if (page.oldest && first < page.oldest) { page.lost = page.oldest - first; first = page.oldest; }
    page.next = after;
    while (first <= page.latest && page.count < capacity) {
        entries[page.count++] = ring[(first - 1) % GW_LOG_CAPACITY];
        page.next = first++;
    }
    leave();
    return page;
}
