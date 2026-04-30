#pragma once

#include <stdarg.h>
#include "sys/types.h"

int vsnprintf(char *dst, uint32_t size, const char *fmt, va_list ap);
int snprintf(char *dst, uint32_t size, const char *fmt, ...);
int vdprintf(int fd, const char *fmt, va_list ap);
int dprintf(int fd, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int veprintf(const char *fmt, va_list ap);
int eprintf(const char *fmt, ...);
int vfdprintf(uint32_t fd, const char *fmt, va_list ap);
int fdprintf(uint32_t fd, const char *fmt, ...);
