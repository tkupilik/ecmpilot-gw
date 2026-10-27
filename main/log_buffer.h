// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
#define GW_LOG_CAPACITY 64
#define GW_LOG_TEXT_SIZE 256
#define GW_LOG_PAGE_SIZE 16
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t sequence; char text[GW_LOG_TEXT_SIZE]; } gw_log_entry_t;
typedef struct { uint64_t oldest, latest, next, lost; size_t count; } gw_log_page_t;
// Reset only at startup, before installing the capture hook.
void gw_log_buffer_reset(void);
void gw_log_buffer_append(const char *text);
gw_log_page_t gw_log_buffer_read(uint64_t after, gw_log_entry_t *entries, size_t capacity);
void gw_log_start(void);
const char *gw_log_boot_id(void);
#ifdef __cplusplus
}
#endif
