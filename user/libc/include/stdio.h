#pragma once

#include <stdarg.h>
#include "sys/types.h"
#include "user/public/sysapi.h"

#ifndef EOF
#define EOF (-1)
#endif

#define SEEK_SET SYS_SEEK_SET
#define SEEK_CUR SYS_SEEK_CUR
#define SEEK_END SYS_SEEK_END

typedef struct nlibc_FILE {
    int fd;
    int owned;
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
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *dst, int size, FILE *stream);
int getchar(void);
int putchar(int ch);
int puts(const char *text);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
void clear(void);
