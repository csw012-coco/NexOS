#include "user/apps/elf/ush/ush_vars_internal.h"

uint32_t ush_vars_strlen(const char *text) {
    uint32_t len = 0;

    while (text != NULL && text[len] != '\0') {
        len++;
    }
    return len;
}

void ush_vars_copy_line(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (dst == NULL || max_len == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int ush_vars_streq(const char *a, const char *b) {
    uint32_t i = 0;

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

int ush_vars_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

void ush_vars_trim_in_place(char *text) {
    uint32_t start = 0;
    uint32_t end = ush_vars_strlen(text);
    uint32_t i = 0;

    while (text[start] != '\0' && ush_vars_is_space(text[start])) {
        start++;
    }
    while (end > start && ush_vars_is_space(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

int ush_var_name_char_local(char ch, int first) {
    if (ch >= 'A' && ch <= 'Z') {
        return 1;
    }
    if (ch >= 'a' && ch <= 'z') {
        return 1;
    }
    if (ch == '_') {
        return 1;
    }
    return !first && ch >= '0' && ch <= '9';
}

int ush_var_name_valid_local(const char *name) {
    uint32_t i = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    while (name[i] != '\0') {
        if (!ush_var_name_char_local(name[i], i == 0u)) {
            return 0;
        }
        i++;
    }
    return 1;
}
