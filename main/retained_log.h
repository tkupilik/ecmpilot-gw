// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdbool.h>
#include "log_buffer.h"

#define GW_RETAINED_LOG_CAPACITY 12

// Initialize once before installing the log hook. The append caller serializes
// writers with the live log lock; the previous bank is immutable after boot.
void gw_retained_log_boot(bool preserve);
void gw_retained_log_append(const char *text);
gw_log_page_t gw_retained_log_read(uint64_t after, gw_log_entry_t *entries, size_t capacity);
unsigned gw_retained_log_count(void);
unsigned gw_retained_log_boot_number(void);
