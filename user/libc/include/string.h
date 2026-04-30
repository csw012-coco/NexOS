#pragma once

#include <stddef.h>

size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t count);
char *strchr(const char *text, int ch);
char *strrchr(const char *text, int ch);
void strlcpy(char *dst, size_t dst_size, const char *src);
