#pragma once

#include <stdint.h>

static inline int vfs_is_decimal_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static inline void vfs_terminate_text(char *dst, uint32_t pos, uint32_t dst_size) {
    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return;
    }
    dst[pos] = '\0';
}

static inline uint32_t vfs_append_text(char *dst, uint32_t pos, uint32_t dst_size, const char *text) {
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (text != 0 && *text != '\0' && pos < dst_size - 1u) {
        dst[pos++] = *text++;
    }
    vfs_terminate_text(dst, pos, dst_size);
    return pos;
}

static inline uint32_t vfs_append_u32_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    char digits[10];
    uint32_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && count < sizeof(digits));
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (count > 0 && pos < dst_size - 1u) {
        dst[pos++] = digits[--count];
    }
    vfs_terminate_text(dst, pos, dst_size);
    return pos;
}

static inline uint32_t vfs_append_i32_text(char *dst, uint32_t pos, uint32_t dst_size, int32_t value) {
    if (value < 0) {
        pos = vfs_append_text(dst, pos, dst_size, "-");
        return vfs_append_u32_text(dst, pos, dst_size, (uint32_t)(-value));
    }
    return vfs_append_u32_text(dst, pos, dst_size, (uint32_t)value);
}

static inline uint32_t vfs_append_padded2_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    if (value < 10u) {
        pos = vfs_append_text(dst, pos, dst_size, "0");
    }
    return vfs_append_u32_text(dst, pos, dst_size, value);
}

static inline uint32_t vfs_append_hex_u32_text(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";

    pos = vfs_append_text(dst, pos, dst_size, "0x");
    for (int i = 7; i >= 0; i--) {
        char digit = hex[(value >> ((uint32_t)i * 4u)) & 0x0fu];

        if (dst != 0 && dst_size != 0 && pos < dst_size - 1u) {
            dst[pos++] = digit;
            dst[pos] = '\0';
        }
    }
    return pos;
}

static inline uint32_t vfs_append_bool_text(char *dst, uint32_t pos, uint32_t dst_size, uint8_t value) {
    return vfs_append_text(dst, pos, dst_size, value ? "1" : "0");
}

static inline uint32_t vfs_append_keyboard_ascii_text(char *dst, uint32_t pos, uint32_t dst_size, char ascii) {
    if (ascii == 0) {
        return vfs_append_text(dst, pos, dst_size, "none");
    }
    if (ascii == '\n') {
        return vfs_append_text(dst, pos, dst_size, "\\n");
    }
    if (ascii == '\t') {
        return vfs_append_text(dst, pos, dst_size, "\\t");
    }
    if (ascii == '\b') {
        return vfs_append_text(dst, pos, dst_size, "\\b");
    }
    if (ascii == ' ') {
        return vfs_append_text(dst, pos, dst_size, "space");
    }
    if ((uint8_t)ascii < 32u || (uint8_t)ascii >= 127u) {
        return vfs_append_text(dst, pos, dst_size, "control");
    }
    if (dst != 0 && dst_size != 0 && pos < dst_size - 1u) {
        dst[pos++] = ascii;
        vfs_terminate_text(dst, pos, dst_size);
    }
    return pos;
}

static inline uint32_t vfs_append_json_string(char *dst, uint32_t pos, uint32_t dst_size, const char *text) {
    pos = vfs_append_text(dst, pos, dst_size, "\"");
    while (text != 0 && *text != '\0' && dst != 0 && dst_size > 2u && pos < dst_size - 2u) {
        if (*text == '"' || *text == '\\') {
            if (dst_size <= 3u || pos >= dst_size - 3u) {
                break;
            }
            dst[pos++] = '\\';
            dst[pos++] = *text++;
            dst[pos] = '\0';
        } else {
            dst[pos++] = *text++;
            dst[pos] = '\0';
        }
    }
    return vfs_append_text(dst, pos, dst_size, "\"");
}

static inline void vfs_copy_event_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i < dst_size - 1u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline uint32_t vfs_append_padded_text(char *dst,
                                              uint32_t pos,
                                              uint32_t dst_size,
                                              const char *text,
                                              uint32_t width) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    pos = vfs_append_text(dst, pos, dst_size, text);
    if (dst == 0 || dst_size == 0) {
        return pos;
    }
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return dst_size - 1u;
    }
    while (len < width && pos < dst_size - 1u) {
        dst[pos++] = ' ';
        dst[pos] = '\0';
        len++;
    }
    return pos;
}

static inline int vfs_parse_u32_full(const char *text, uint32_t *out) {
    uint32_t value = 0;

    if (text == 0 || *text == '\0' || out == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (!vfs_is_decimal_digit(*text)) {
            return 0;
        }
        value = value * 10u + (uint32_t)(*text - '0');
        text++;
    }
    *out = value;
    return 1;
}

static inline int64_t vfs_read_from_generated_text(uint32_t *offset_io,
                                                   void *buffer,
                                                   uint32_t size,
                                                   const char *text,
                                                   uint32_t text_size) {
    uint32_t copied = 0;
    uint8_t *out = (uint8_t *)buffer;

    if (offset_io == 0 || buffer == 0 || text == 0) {
        return -1;
    }
    if (*offset_io >= text_size) {
        return 0;
    }
    while (copied < size && *offset_io + copied < text_size) {
        out[copied] = (uint8_t)text[*offset_io + copied];
        copied++;
    }
    *offset_io += copied;
    return (int64_t)copied;
}
