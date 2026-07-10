#include <stddef.h>
#include <stdlib.h>
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
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t count) {
    if (count == 0u) {
        return 0;
    }
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
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

char *strcpy(char *dst, const char *src) {
    size_t i = 0;

    if (dst == 0 || src == 0) {
        return dst;
    }
    do {
        dst[i] = src[i];
    } while (src[i++] != '\0');
    return dst;
}

char *strncpy(char *dst, const char *src, size_t count) {
    size_t i = 0;

    if (dst == 0 || src == 0) {
        return dst;
    }
    while (i < count && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < count) {
        dst[i++] = '\0';
    }
    return dst;
}

char *strcat(char *dst, const char *src) {
    if (dst == 0 || src == 0) {
        return dst;
    }
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strchr(const char *text, int ch) {
    char needle = (char)ch;

    if (text == 0) {
        return 0;
    }
    for (;;) {
        if (*text == needle) {
            return (char *)text;
        }
        if (*text == '\0') {
            return 0;
        }
        text++;
    }
}

char *strrchr(const char *text, int ch) {
    char needle = (char)ch;
    char *last = 0;

    if (text == 0) {
        return 0;
    }
    for (;;) {
        if (*text == needle) {
            last = (char *)text;
        }
        if (*text == '\0') {
            return last;
        }
        text++;
    }
}

char *strstr(const char *text, const char *needle) {
    size_t needle_len;

    if (text == 0 || needle == 0) {
        return 0;
    }
    needle_len = strlen(needle);
    if (needle_len == 0u) {
        return (char *)text;
    }
    while (*text != '\0') {
        if (strncmp(text, needle, needle_len) == 0) {
            return (char *)text;
        }
        text++;
    }
    return 0;
}

char *strdup(const char *text) {
    size_t size;
    char *copy;

    if (text == 0) {
        return 0;
    }
    size = strlen(text) + 1u;
    copy = malloc(size);
    if (copy != 0) {
        memcpy(copy, text, size);
    }
    return copy;
}

void strlcpy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void trim_line(char *text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        if (*text == '\n' || *text == '\r') {
            *text = '\0';
            return;
        }
        text++;
    }
}

int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

int starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) {
        return 0;
    }
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

int ends_with(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;

    if (text == 0 || suffix == 0) {
        return 0;
    }
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    return suffix_len <= text_len &&
           strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char ascii_tolower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

int strcasecmp(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    for (;;) {
        char ca = ascii_tolower(*a);
        char cb = ascii_tolower(*b);

        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        if (*a == '\0') {
            return 0;
        }
        a++;
        b++;
    }
}

int strncasecmp(const char *a, const char *b, unsigned long count) {
    if (count == 0u) {
        return 0;
    }
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    while (count != 0u && *a != '\0' && *b != '\0') {
        char ca = ascii_tolower(*a);
        char cb = ascii_tolower(*b);

        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        a++;
        b++;
        count--;
    }
    if (count == 0u) {
        return 0;
    }
    return (int)(unsigned char)ascii_tolower(*a) -
           (int)(unsigned char)ascii_tolower(*b);
}
