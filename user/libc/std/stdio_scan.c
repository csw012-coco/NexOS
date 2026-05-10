#include "user/libc/include/nlibc.h"
#include <stdarg.h>

enum scan_length {
    SCAN_LEN_DEFAULT = 0,
    SCAN_LEN_H,
    SCAN_LEN_HH,
    SCAN_LEN_L,
    SCAN_LEN_LL
};

static int scan_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

static int scan_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 10;
    }
    return -1;
}

static void scan_skip_input_space(const char **cursor_io) {
    const char *cursor = *cursor_io;

    while (*cursor != '\0' && scan_is_space(*cursor)) {
        cursor++;
    }
    *cursor_io = cursor;
}

static int scan_width_take(uint32_t *used, uint32_t width) {
    return width == 0u || *used < width;
}

static int scan_can_read(const char *cursor, uint32_t used, uint32_t width) {
    return *cursor != '\0' && (width == 0u || used < width);
}

static int scan_parse_unsigned(const char **cursor_io,
                               uint32_t width,
                               uint32_t base,
                               int allow_sign,
                               int *negative_out,
                               uint64_t *value_out) {
    const char *cursor = *cursor_io;
    uint32_t used = 0u;
    uint64_t value = 0u;
    int negative = 0;
    int any = 0;

    if (negative_out != 0) {
        *negative_out = 0;
    }
    if (value_out != 0) {
        *value_out = 0u;
    }
    if (allow_sign && scan_can_read(cursor, used, width) && (*cursor == '+' || *cursor == '-')) {
        negative = *cursor == '-';
        cursor++;
        used++;
    }
    if (base == 0u) {
        base = 10u;
        if (scan_can_read(cursor, used, width) && cursor[0] == '0') {
            base = 8u;
            if ((width == 0u || used + 2u < width) &&
                (cursor[1] == 'x' || cursor[1] == 'X') &&
                scan_digit_value(cursor[2]) >= 0 &&
                scan_digit_value(cursor[2]) < 16) {
                base = 16u;
                cursor += 2;
                used += 2u;
            }
        }
    } else if (base == 16u &&
               scan_can_read(cursor, used, width) &&
               cursor[0] == '0' &&
               (width == 0u || used + 2u < width) &&
               (cursor[1] == 'x' || cursor[1] == 'X') &&
               scan_digit_value(cursor[2]) >= 0 &&
               scan_digit_value(cursor[2]) < 16) {
        cursor += 2;
        used += 2u;
    }
    while (scan_can_read(cursor, used, width)) {
        int digit = scan_digit_value(*cursor);

        if (digit < 0 || (uint32_t)digit >= base) {
            break;
        }
        value = value * (uint64_t)base + (uint64_t)digit;
        any = 1;
        cursor++;
        used++;
    }
    if (!any) {
        return 0;
    }
    *cursor_io = cursor;
    if (negative_out != 0) {
        *negative_out = negative;
    }
    if (value_out != 0) {
        *value_out = value;
    }
    return 1;
}

static void scan_store_signed(va_list ap, enum scan_length length, int64_t value) {
    switch (length) {
        case SCAN_LEN_LL:
            *va_arg(ap, long long *) = (long long)value;
            break;
        case SCAN_LEN_L:
            *va_arg(ap, long *) = (long)value;
            break;
        case SCAN_LEN_H:
            *va_arg(ap, short *) = (short)value;
            break;
        case SCAN_LEN_HH:
            *va_arg(ap, signed char *) = (signed char)value;
            break;
        default:
            *va_arg(ap, int *) = (int)value;
            break;
    }
}

static void scan_store_unsigned(va_list ap, enum scan_length length, uint64_t value) {
    switch (length) {
        case SCAN_LEN_LL:
            *va_arg(ap, unsigned long long *) = (unsigned long long)value;
            break;
        case SCAN_LEN_L:
            *va_arg(ap, unsigned long *) = (unsigned long)value;
            break;
        case SCAN_LEN_H:
            *va_arg(ap, unsigned short *) = (unsigned short)value;
            break;
        case SCAN_LEN_HH:
            *va_arg(ap, unsigned char *) = (unsigned char)value;
            break;
        default:
            *va_arg(ap, unsigned int *) = (unsigned int)value;
            break;
    }
}

int vsscanf(const char *text, const char *fmt, va_list ap) {
    const char *input = text;
    int assigned = 0;

    if (text == 0 || fmt == 0) {
        return 0;
    }
    while (*fmt != '\0') {
        int suppress = 0;
        uint32_t width = 0u;
        enum scan_length length = SCAN_LEN_DEFAULT;
        char conv;

        if (scan_is_space(*fmt)) {
            while (scan_is_space(*fmt)) {
                fmt++;
            }
            scan_skip_input_space(&input);
            continue;
        }
        if (*fmt != '%') {
            if (*input != *fmt) {
                break;
            }
            input++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (*input != '%') {
                break;
            }
            input++;
            fmt++;
            continue;
        }
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (uint32_t)(*fmt - '0');
            fmt++;
        }
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') {
                length = SCAN_LEN_HH;
                fmt++;
            } else {
                length = SCAN_LEN_H;
            }
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                length = SCAN_LEN_LL;
                fmt++;
            } else {
                length = SCAN_LEN_L;
            }
        }
        conv = *fmt;
        if (conv == '\0') {
            break;
        }
        fmt++;

        if (conv == 'c') {
            uint32_t count = width != 0u ? width : 1u;
            char *out = suppress ? 0 : va_arg(ap, char *);

            if (*input == '\0') {
                break;
            }
            for (uint32_t i = 0u; i < count; i++) {
                if (*input == '\0') {
                    return assigned;
                }
                if (out != 0) {
                    out[i] = *input;
                }
                input++;
            }
            if (!suppress) {
                assigned++;
            }
            continue;
        }
        if (conv == 's') {
            char *out = suppress ? 0 : va_arg(ap, char *);
            uint32_t count = 0u;

            scan_skip_input_space(&input);
            while (*input != '\0' && !scan_is_space(*input) && scan_width_take(&count, width)) {
                if (out != 0) {
                    out[count] = *input;
                }
                input++;
                count++;
            }
            if (count == 0u) {
                break;
            }
            if (out != 0) {
                out[count] = '\0';
            }
            if (!suppress) {
                assigned++;
            }
            continue;
        }
        if (conv == 'n') {
            if (!suppress) {
                scan_store_signed(ap, length, (int64_t)(input - text));
            }
            continue;
        }
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o' || conv == 'p') {
            uint32_t base = 10u;
            uint64_t value = 0u;
            int negative = 0;

            scan_skip_input_space(&input);
            if (conv == 'i') {
                base = 0u;
            } else if (conv == 'x' || conv == 'X' || conv == 'p') {
                base = 16u;
            } else if (conv == 'o') {
                base = 8u;
            }
            if (!scan_parse_unsigned(&input, width, base, 1, &negative, &value)) {
                break;
            }
            if (!suppress) {
                if (conv == 'd' || conv == 'i') {
                    int64_t signed_value = negative ? -(int64_t)value : (int64_t)value;

                    scan_store_signed(ap, length, signed_value);
                } else if (conv == 'p') {
                    *va_arg(ap, void **) = (void *)(uintptr_t)(negative ? (uint64_t)(0u - value) : value);
                } else {
                    if (negative) {
                        value = 0u - value;
                    }
                    scan_store_unsigned(ap, length, value);
                }
                assigned++;
            }
            continue;
        }
        break;
    }
    return assigned;
}

int sscanf(const char *text, const char *fmt, ...) {
    va_list ap;
    int result;

    va_start(ap, fmt);
    result = vsscanf(text, fmt, ap);
    va_end(ap);
    return result;
}
