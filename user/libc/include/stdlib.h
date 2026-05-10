#pragma once

#include <stddef.h>

extern char **environ;

unsigned long strtoul(const char *text, char **endptr, int base);
unsigned long long strtoull(const char *text, char **endptr, int base);
int atoi(const char *text);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
