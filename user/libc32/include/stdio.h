#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct FILE {
    int fd;
    int error;
    int eof;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int ch);
int puts(const char *text);
FILE *fopen(const char *path, const char *mode);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fclose(FILE *stream);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list args);
int printf(const char *format, ...);
