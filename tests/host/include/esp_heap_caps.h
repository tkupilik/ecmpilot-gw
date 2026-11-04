// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT 1U
#define MALLOC_CAP_INTERNAL 2U
#define MALLOC_CAP_DMA 4U
#define MALLOC_CAP_SPIRAM 8U
typedef struct {
    size_t total_free_bytes, total_allocated_bytes, largest_free_block,
           minimum_free_bytes, allocated_blocks, free_blocks, total_blocks;
} multi_heap_info_t;
void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps);
