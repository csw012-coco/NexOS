#include "user/libc/include/nlibc.h"

static int stdlib_digit_value_local(char ch) {
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

static const char *stdlib_skip_space_local(const char *text) {
    if (text == 0) {
        return 0;
    }
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r' || *text == '\f' || *text == '\v') {
        text++;
    }
    return text;
}

unsigned long strtoul(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space_local(text);
    unsigned long value = 0;
    int any = 0;

    if (cursor == 0) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0;
    }
    if (*cursor == '+') {
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    while (*cursor != '\0') {
        int digit = stdlib_digit_value_local(*cursor);

        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
        any = 1;
        cursor++;
    }
    if (endptr != 0) {
        *endptr = (char *)(any ? cursor : text);
    }
    return value;
}

unsigned long long strtoull(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space_local(text);
    unsigned long long value = 0;
    int any = 0;

    if (cursor == 0) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0;
    }
    if (*cursor == '+') {
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    while (*cursor != '\0') {
        int digit = stdlib_digit_value_local(*cursor);

        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long long)base + (unsigned long long)digit;
        any = 1;
        cursor++;
    }
    if (endptr != 0) {
        *endptr = (char *)(any ? cursor : text);
    }
    return value;
}

int atoi(const char *text) {
    const char *cursor = stdlib_skip_space_local(text);
    int sign = 1;
    unsigned long value;

    if (cursor == 0) {
        return 0;
    }
    if (*cursor == '-') {
        sign = -1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }
    value = strtoul(cursor, 0, 10);
    return sign < 0 ? -(int)value : (int)value;
}
