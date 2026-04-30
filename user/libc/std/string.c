#include "user/libc/include/nlibc.h"

size_t strlen(const char *text) {
    size_t len = 0;

    if (text == 0) {
        return 0;
    }
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *a, const char *b) {
    size_t i = 0;

    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

int strncmp(const char *a, const char *b, size_t count) {
    size_t i = 0;

    if (count == 0u) {
        return 0;
    }
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    while (i < count && a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
        i++;
    }
    if (i == count) {
        return 0;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

char *strchr(const char *text, int ch) {
    char needle = (char)ch;
    size_t i = 0;

    if (text == 0) {
        return 0;
    }
    for (;;) {
        if (text[i] == needle) {
            return (char *)(text + i);
        }
        if (text[i] == '\0') {
            return 0;
        }
        i++;
    }
}

char *strrchr(const char *text, int ch) {
    char needle = (char)ch;
    char *last = 0;
    size_t i = 0;

    if (text == 0) {
        return 0;
    }
    for (;;) {
        if (text[i] == needle) {
            last = (char *)(text + i);
        }
        if (text[i] == '\0') {
            return last;
        }
        i++;
    }
}

void strlcpy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void trim_line(char *text) {
    uint32_t i = 0;

    if (text == 0) {
        return;
    }
    while (text[i] != '\0') {
        if (text[i] == '\n' || text[i] == '\r') {
            text[i] = '\0';
            return;
        }
        i++;
    }
}

int streq(const char *a, const char *b) {
    uint32_t i = 0;

    if (a == 0 || b == 0) {
        return a == b;
    }
    for (;;) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
}

int starts_with(const char *text, const char *prefix) {
    uint32_t i = 0;

    if (text == 0 || prefix == 0) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static char ascii_tolower_local(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

int strcasecmp(const char *a, const char *b) {
    uint32_t i = 0;

    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    for (;;) {
        char ca = ascii_tolower_local(a[i]);
        char cb = ascii_tolower_local(b[i]);

        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        if (a[i] == '\0') {
            return 0;
        }
        i++;
    }
}

int strncasecmp(const char *a, const char *b, unsigned long count) {
    unsigned long i = 0;

    if (count == 0u) {
        return 0;
    }
    if (a == 0 || b == 0) {
        return a == b ? 0 : (a == 0 ? -1 : 1);
    }
    while (i < count && a[i] != '\0' && b[i] != '\0') {
        char ca = ascii_tolower_local(a[i]);
        char cb = ascii_tolower_local(b[i]);

        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        i++;
    }
    if (i == count) {
        return 0;
    }
    return (int)(unsigned char)ascii_tolower_local(a[i]) -
           (int)(unsigned char)ascii_tolower_local(b[i]);
}

int ends_with(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;
    size_t i;

    if (text == 0 || suffix == 0) {
        return 0;
    }
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; i++) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}
