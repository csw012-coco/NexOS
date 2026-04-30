#include "lib/string.h"

uint32_t str_len(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

int streq(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}

void *memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
    return dst;
}

void *memset(void *dst, int value, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = (uint8_t)value;
    }
    return dst;
}
