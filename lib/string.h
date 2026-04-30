#pragma once

#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

uint32_t str_len(const char *text);
int streq(const char *lhs, const char *rhs);
int starts_with(const char *text, const char *prefix);
const char *skip_spaces(const char *text);
void *memcpy(void *dst, const void *src, uint32_t size);
void *memset(void *dst, int value, uint32_t size);
