#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static void emit_char(char *buffer, size_t size, size_t *position, char value) {
    if (buffer != 0 && *position + 1u < size) {
        buffer[*position] = value;
    }
    (*position)++;
}

static void emit_string(char *buffer,
                        size_t size,
                        size_t *position,
                        const char *text) {
    if (text == 0) {
        text = "(null)";
    }
    while (*text != '\0') {
        emit_char(buffer, size, position, *text++);
    }
}

static void emit_unsigned(char *buffer,
                          size_t size,
                          size_t *position,
                          uint32_t value,
                          uint32_t base,
                          int uppercase,
                          size_t width,
                          char padding) {
    char digits_buffer[16];
    const char *digits = uppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
    size_t count = 0u;

    do {
        digits_buffer[count++] = digits[value % base];
        value /= base;
    } while (value != 0u);
    while (width > count) {
        emit_char(buffer, size, position, padding);
        width--;
    }
    while (count != 0u) {
        emit_char(buffer, size, position, digits_buffer[--count]);
    }
}

int vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    size_t position = 0u;

    if (format == 0) {
        return -1;
    }
    while (*format != '\0') {
        size_t width = 0u;
        char padding = ' ';

        if (*format != '%') {
            emit_char(buffer, size, &position, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            emit_char(buffer, size, &position, *format++);
            continue;
        }
        if (*format == '0') {
            padding = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10u + (size_t)(*format++ - '0');
        }
        switch (*format) {
            case 'c':
                emit_char(buffer, size, &position, (char)va_arg(args, int));
                break;
            case 's':
                emit_string(buffer,
                            size,
                            &position,
                            va_arg(args, const char *));
                break;
            case 'd':
            case 'i': {
                int32_t value = va_arg(args, int32_t);
                uint32_t magnitude;

                if (value < 0) {
                    emit_char(buffer, size, &position, '-');
                    magnitude = (uint32_t)(-(value + 1)) + 1u;
                } else {
                    magnitude = (uint32_t)value;
                }
                emit_unsigned(buffer,
                              size,
                              &position,
                              magnitude,
                              10u,
                              0,
                              width,
                              padding);
                break;
            }
            case 'u':
                emit_unsigned(buffer,
                              size,
                              &position,
                              va_arg(args, uint32_t),
                              10u,
                              0,
                              width,
                              padding);
                break;
            case 'x':
            case 'X':
                emit_unsigned(buffer,
                              size,
                              &position,
                              va_arg(args, uint32_t),
                              16u,
                              *format == 'X',
                              width,
                              padding);
                break;
            case 'p':
                emit_string(buffer, size, &position, "0x");
                emit_unsigned(buffer,
                              size,
                              &position,
                              (uint32_t)(uintptr_t)va_arg(args, void *),
                              16u,
                              0,
                              8u,
                              '0');
                break;
            default:
                emit_char(buffer, size, &position, '%');
                if (*format != '\0') {
                    emit_char(buffer, size, &position, *format);
                }
                break;
        }
        if (*format != '\0') {
            format++;
        }
    }
    if (buffer != 0 && size != 0u) {
        buffer[position < size ? position : size - 1u] = '\0';
    }
    return position > 0x7fffffffu ? -1 : (int)position;
}

int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int vprintf(const char *format, va_list args) {
    char buffer[256];
    int result = vsnprintf(buffer, sizeof(buffer), format, args);
    size_t length;

    if (result < 0) {
        return result;
    }
    length = (size_t)result;
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1u;
    }
    return write(STDOUT_FILENO, buffer, length) == (ssize_t)length
        ? result
        : -1;
}

int printf(const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vprintf(format, args);
    va_end(args);
    return result;
}
