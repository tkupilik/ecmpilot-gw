// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
void *malloc(size_t);
void free(void *);
void *realloc(void *, size_t);
double strtod(const char *, char **);
