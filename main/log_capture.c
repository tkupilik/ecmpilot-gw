// SPDX-License-Identifier: BSD-2-Clause
#include "log_buffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

static vprintf_like_t serial_output = vprintf;
static char boot_id[33];
static int capture(const char *format, va_list args)
{
    char text[GW_LOG_TEXT_SIZE];
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(text, sizeof(text), format, copy);
    va_end(copy);
    if (length > 0) {
        if ((size_t)length >= sizeof(text)) {
            static const char marker[] = " [truncated]\n";
            size_t end = sizeof(text) - sizeof(marker);
            // Do not split a UTF-8 codepoint immediately before the marker.
            while (end && ((unsigned char)text[end] & 0xc0) == 0x80) --end;
            memcpy(text + end, marker, sizeof(marker));
        }
        gw_log_buffer_append(text);
    }
    // Keep console output independent of the short ring-buffer lock. ESP may
    // invoke this callback concurrently from both cores; no heap/network here.
    vprintf_like_t sink = __atomic_load_n(&serial_output, __ATOMIC_ACQUIRE);
    va_copy(copy, args);
    int result = sink ? sink(format, copy) : length;
    va_end(copy);
    return result;
}
void gw_log_start(void)
{
    // Called once, as the first app_main operation.
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < sizeof(random); ++i) snprintf(boot_id + 2 * i, 3, "%02x", random[i]);
    gw_log_buffer_reset();
    vprintf_like_t previous = esp_log_set_vprintf(capture);
    __atomic_store_n(&serial_output, previous, __ATOMIC_RELEASE);
}
const char *gw_log_boot_id(void) { return boot_id; }
