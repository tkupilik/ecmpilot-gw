// SPDX-License-Identifier: BSD-2-Clause
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
int _fltused;
int vsnprintf(char *, size_t, const char *, va_list);
int _vscprintf(const char *, va_list);
int gw_test_vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    // Supply C99 return/termination semantics over the older Windows CRT ABI.
    va_list copy; va_copy(copy, args);
    int result = _vscprintf(format, copy); va_end(copy);
    if (size) {
        va_copy(copy, args); (void)vsnprintf(buffer, size, format, copy); va_end(copy);
        if (result >= 0 && (size_t)result >= size) buffer[size - 1] = 0;
    }
    return result;
}
int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = gw_test_vsnprintf(buffer, size, format, args);
    va_end(args); return result;
}
static uint64_t now_ms;
void gw_test_set_time(uint64_t time) { now_ms = time; }
int64_t esp_timer_get_time(void) { return (int64_t)(now_ms * 1000); }
