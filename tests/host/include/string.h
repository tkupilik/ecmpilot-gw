// SPDX-License-Identifier: BSD-2-Clause
// Test-only MSVCRT ABI declarations for Clang without an installed Windows SDK.
#pragma once
#include <stddef.h>
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
size_t strspn(const char *, const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strcpy(char *, const char *);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
