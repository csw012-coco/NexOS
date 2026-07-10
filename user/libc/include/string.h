#pragma once

#include <stddef.h>

size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);
void *memset(void *dst, int value, size_t count);
int memcmp(const void *a, const void *b, size_t count);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t count);
char *strcat(char *dst, const char *src);
char *strchr(const char *text, int ch);
char *strrchr(const char *text, int ch);
char *strstr(const char *text, const char *needle);
char *strdup(const char *text);
void strlcpy(char *dst, size_t dst_size, const char *src);
void trim_line(char *text);
int streq(const char *a, const char *b);
int starts_with(const char *text, const char *prefix);
int ends_with(const char *text, const char *suffix);
