#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

int putchar(int ch);
int puts(const char *text);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vprintf(const char *format, va_list args);
int printf(const char *format, ...);
