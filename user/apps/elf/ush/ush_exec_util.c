#include "user/apps/elf/ush/ush_exec_internal.h"

int streq_local(const char *a, const char *b) {
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

int starts_with_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

int contains_char_local(const char *text, char ch) {
    uint32_t i = 0;

    while (text[i] != '\0') {
        if (text[i] == ch) {
            return 1;
        }
        i++;
    }
    return 0;
}

uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

void copy_line_local(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (max_len == 0) {
        return;
    }
    while (src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char to_upper_ascii_local(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

void upper_in_place_local(char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[i] != '\0') {
        text[i] = to_upper_ascii_local(text[i]);
        i++;
    }
}

static char to_lower_ascii_local(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }
    return ch;
}

void lower_in_place_local(char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[i] != '\0') {
        text[i] = to_lower_ascii_local(text[i]);
        i++;
    }
}

int ends_with_ignore_case_local(const char *text, const char *suffix) {
    uint32_t text_len = str_len_local(text);
    uint32_t suffix_len = str_len_local(suffix);
    uint32_t i;

    if (suffix_len > text_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; i++) {
        if (to_upper_ascii_local(text[text_len - suffix_len + i]) != to_upper_ascii_local(suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_space_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

const char *skip_spaces_local(const char *text) {
    while (text != NULL && is_space_local(*text)) {
        text++;
    }
    return text;
}

void trim_in_place_local(char *text) {
    uint32_t start = 0;
    uint32_t end = str_len_local(text);
    uint32_t i = 0;

    while (text[start] != '\0' && is_space_local(text[start])) {
        start++;
    }
    while (end > start && is_space_local(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

int read_token_local(const char **text_io, char *out, uint32_t out_size) {
    uint32_t len = 0;
    const char *text;

    if (text_io == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    text = skip_spaces_local(*text_io);
    if (text == NULL || *text == '\0') {
        out[0] = '\0';
        *text_io = text;
        return 0;
    }
    while (*text != '\0' && !is_space_local(*text)) {
        if (len + 1u >= out_size) {
            return 0;
        }
        out[len++] = *text++;
    }
    out[len] = '\0';
    *text_io = text;
    return 1;
}

int ush_parse_exit_code_local(const char *text, uint64_t *code_out) {
    char token[32];
    const char *cursor = skip_spaces_local(text);
    char *end = 0;
    unsigned long value;

    if (code_out == NULL) {
        return 0;
    }
    if (cursor == NULL || *cursor == '\0') {
        *code_out = 0u;
        return 1;
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        return 0;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        return 0;
    }
    value = strtoul(token, &end, 0);
    if (end == token || *end != '\0') {
        return 0;
    }
    *code_out = (uint64_t)value;
    return 1;
}
