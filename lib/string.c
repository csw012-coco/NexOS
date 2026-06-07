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
    uint64_t qwords = size / sizeof(uint64_t);
    uint64_t bytes = size % sizeof(uint64_t);

    __asm__ volatile (
        "cld\n\t"
        "rep movsq\n\t"
        "mov %[bytes], %%rcx\n\t"
        "rep movsb"
        : "+D"(out), "+S"(in), "+c"(qwords)
        : [bytes] "r"(bytes)
        : "memory", "cc"
    );
    return dst;
}

void *memset(void *dst, int value, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;

    for (uint32_t i = 0; i < size; i++) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

void *memmove(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    uint64_t qwords;
    uint64_t bytes;

    if (out == in || size == 0u) {
        return dst;
    }

    if (out < in || out >= in + size) {
        return memcpy(dst, src, size);
    }

    qwords = size / sizeof(uint64_t);
    bytes = size % sizeof(uint64_t);
    if (qwords != 0u) {
        uint8_t *out_end = out + size - sizeof(uint64_t);
        const uint8_t *in_end = in + size - sizeof(uint64_t);

        __asm__ volatile (
            "std\n\t"
            "rep movsq\n\t"
            "cld"
            : "+D"(out_end), "+S"(in_end), "+c"(qwords)
            :
            : "memory", "cc"
        );
    }
    if (bytes != 0u) {
        uint8_t *out_end = out + bytes - 1u;
        const uint8_t *in_end = in + bytes - 1u;

        __asm__ volatile (
            "std\n\t"
            "rep movsb\n\t"
            "cld"
            : "+D"(out_end), "+S"(in_end), "+c"(bytes)
            :
            : "memory", "cc"
        );
    }
    return dst;
}
