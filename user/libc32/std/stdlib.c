#include <stdlib.h>

int errno;

static int stdlib_digit_value(char ch) {
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

static const char *stdlib_skip_space(const char *text) {
    if (text == 0) {
        return 0;
    }
    while (*text == ' ' ||
           *text == '\t' ||
           *text == '\n' ||
           *text == '\r' ||
           *text == '\f' ||
           *text == '\v') {
        text++;
    }
    return text;
}

static int stdlib_base_valid(int base) {
    return base == 0 || (base >= 2 && base <= 36);
}

unsigned long strtoul(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space(text);
    const char *start = text;
    unsigned long value = 0u;
    int negative = 0;
    int any = 0;

    if (cursor == 0 || !stdlib_base_valid(base)) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0u;
    }
    start = cursor;
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
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
    } else if (base == 16 &&
               cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    while (*cursor != '\0') {
        int digit = stdlib_digit_value(*cursor);

        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
        any = 1;
        cursor++;
    }
    if (endptr != 0) {
        *endptr = (char *)(any ? cursor : start);
    }
    return negative ? (unsigned long)(0u - value) : value;
}

unsigned long long strtoull(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space(text);
    const char *start = text;
    unsigned long long value = 0u;
    int negative = 0;
    int any = 0;

    if (cursor == 0 || !stdlib_base_valid(base)) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0u;
    }
    start = cursor;
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
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
    } else if (base == 16 &&
               cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    while (*cursor != '\0') {
        int digit = stdlib_digit_value(*cursor);

        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long long)base +
                (unsigned long long)digit;
        any = 1;
        cursor++;
    }
    if (endptr != 0) {
        *endptr = (char *)(any ? cursor : start);
    }
    return negative ? (unsigned long long)(0u - value) : value;
}

long strtol(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space(text);
    int negative = 0;
    unsigned long value;

    if (cursor == 0) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0;
    }
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
        cursor++;
    }
    value = strtoul(cursor, endptr, base);
    return negative ? -(long)value : (long)value;
}

long long strtoll(const char *text, char **endptr, int base) {
    const char *cursor = stdlib_skip_space(text);
    int negative = 0;
    unsigned long long value;

    if (cursor == 0) {
        if (endptr != 0) {
            *endptr = (char *)text;
        }
        return 0;
    }
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
        cursor++;
    }
    value = strtoull(cursor, endptr, base);
    return negative ? -(long long)value : (long long)value;
}

int atoi(const char *text) {
    return (int)strtol(text, 0, 10);
}

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0 ? -value : value;
}
