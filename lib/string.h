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
void *memmove(void *dst, const void *src, uint32_t size);

void *memcpy_scalar(void *dst, const void *src, uint32_t size);
void *memset_scalar(void *dst, int value, uint32_t size);
void *memcpy_erms(void *dst, const void *src, uint32_t size);
void *memset_erms(void *dst, int value, uint32_t size);
void *memcpy_sse2(void *dst, const void *src, uint32_t size);
void *memset_sse2(void *dst, int value, uint32_t size);
void *memcpy_wc(void *dst, const void *src, uint32_t size);

void string_runtime_init(void);
int string_runtime_has_erms(void);
void string_runtime_set_legacy_thresholds(uint32_t copy_threshold, uint32_t set_threshold);
uint32_t string_runtime_copy_threshold(void);
uint32_t string_runtime_set_threshold(void);
int string_sse2_self_test(void);
void string_memory_benchmark(void);
