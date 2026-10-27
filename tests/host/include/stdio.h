// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stddef.h>
#include <stdarg.h>
int gw_test_vsnprintf(char *, size_t, const char *, va_list);
#define vsnprintf gw_test_vsnprintf
int vprintf(const char *, va_list);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int sscanf(const char *, const char *, ...);
