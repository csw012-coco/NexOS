#pragma once

#include <stdarg.h>
#include "sys/types.h"

#ifndef EOF
#define EOF (-1)
#endif

typedef struct nlibc_FILE {
    int fd;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int vsnprintf(char *dst, uint32_t size, const char *fmt, va_list ap);
int snprintf(char *dst, uint32_t size, const char *fmt, ...);
int vsscanf(const char *text, const char *fmt, va_list ap);
int sscanf(const char *text, const char *fmt, ...);
int vdprintf(int fd, const char *fmt, va_list ap);
int dprintf(int fd, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int veprintf(const char *fmt, va_list ap);
int eprintf(const char *fmt, ...);
int vfdprintf(uint32_t fd, const char *fmt, va_list ap);
int fdprintf(uint32_t fd, const char *fmt, ...);
int fgetc(FILE *stream);
char *fgets(char *dst, int size, FILE *stream);
int getchar(void);
void clear(void);
