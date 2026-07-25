#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE {
    int fd;
    int error;
    int eof;
    int owned;
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
int fflush(FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int getchar(void);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
void clear(void);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vdprintf(int fd, const char *format, va_list args);
int dprintf(int fd, const char *format, ...);
int veprintf(const char *format, va_list args);
int eprintf(const char *format, ...);
int vfdprintf(uint32_t fd, const char *format, va_list args);
int fdprintf(uint32_t fd, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list args);
int printf(const char *format, ...);
int vsscanf(const char *text, const char *format, va_list args);
int sscanf(const char *text, const char *format, ...);
