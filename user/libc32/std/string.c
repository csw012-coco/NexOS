#include <stddef.h>
#include <string.h>

void *memcpy(void *dst, const void *src, size_t count) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;

    for (size_t i = 0; i < count; i++) {
        out[i] = in[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t count) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;

    if (out < in) {
        for (size_t i = 0; i < count; i++) {
            out[i] = in[i];
        }
    } else if (out > in) {
        while (count != 0u) {
            count--;
            out[count] = in[count];
        }
    }
    return dst;
}

void *memset(void *dst, int value, size_t count) {
    unsigned char *out = (unsigned char *)dst;

    for (size_t i = 0; i < count; i++) {
        out[i] = (unsigned char)value;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t count) {
    const unsigned char *left = (const unsigned char *)a;
    const unsigned char *right = (const unsigned char *)b;

    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i]) {
            return (int)left[i] - (int)right[i];
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;

    while (text != 0 && text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t count) {
    while (count != 0u && *a != '\0' && *a == *b) {
        a++;
        b++;
        count--;
    }
    if (count == 0u) {
        return 0;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
