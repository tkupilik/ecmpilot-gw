// SPDX-License-Identifier: BSD-2-Clause
#include "esp_log.h"
#include "esp_random.h"
#include <stdio.h>
static unsigned calls;
static int sink(const char *format, va_list args)
{
    __atomic_fetch_add(&calls, 1, __ATOMIC_RELAXED);
    char text[2048]; return vsnprintf(text, sizeof(text), format, args);
}
static vprintf_like_t callback = sink;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t next)
{
    vprintf_like_t previous = callback; callback = next; return previous;
}
static int emit(const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = callback(format, args); va_end(args); return result;
}
int gw_test_log_emit(const char *text) { return emit("%s", text); }
unsigned gw_test_log_calls(void) { return __atomic_load_n(&calls, __ATOMIC_RELAXED); }
void esp_fill_random(void *data, size_t size)
{
    static unsigned generation;
    unsigned char *p = data; ++generation;
    for (size_t i = 0; i < size; ++i) p[i] = (unsigned char)(generation + i);
}
