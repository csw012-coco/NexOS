#include "bootx.h"

void *memcpy(void *dest, const void *src, size_t len) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t len) {
    uint8_t *d = dest;
    for (size_t i = 0; i < len; i++) {
        d[i] = (uint8_t)value;
    }
    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t len) {
    const uint8_t *a = lhs;
    const uint8_t *b = rhs;
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }
    return (unsigned char)*lhs - (unsigned char)*rhs;
}

int strncmp(const char *lhs, const char *rhs, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (lhs[i] != rhs[i] || lhs[i] == '\0' || rhs[i] == '\0') {
            return (unsigned char)lhs[i] - (unsigned char)rhs[i];
        }
    }
    return 0;
}

char *strchr(const char *str, int ch) {
    while (*str != '\0') {
        if (*str == (char)ch) {
            return (char *)str;
        }
        str++;
    }
    return (ch == 0) ? (char *)str : 0;
}

void strtoupper(char *str) {
    while (*str != '\0') {
        if (*str >= 'a' && *str <= 'z') {
            *str = (char)(*str - ('a' - 'A'));
        }
        str++;
    }
}
