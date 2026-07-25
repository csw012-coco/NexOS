#pragma once

#include <stddef.h>

extern char **environ;

int abs(int value);
long labs(long value);
double atof(const char *text);
int atoi(const char *text);
long strtol(const char *text, char **endptr, int base);
unsigned long strtoul(const char *text, char **endptr, int base);
long long strtoll(const char *text, char **endptr, int base);
unsigned long long strtoull(const char *text, char **endptr, int base);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
__attribute__((noreturn)) void exit(int status);

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
int system(const char *command);
