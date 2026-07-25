#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

int vdprintf(int fd, const char *format, va_list args) {
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
    return write(fd, buffer, length) == (ssize_t)length ? result : -1;
}

int dprintf(int fd, const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vdprintf(fd, format, args);
    va_end(args);
    return result;
}

int veprintf(const char *format, va_list args) {
    return vdprintf(STDERR_FILENO, format, args);
}

int eprintf(const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = veprintf(format, args);
    va_end(args);
    return result;
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    if (stream == 0) {
        return -1;
    }
    {
        int result = vdprintf(stream->fd, format, args);

        if (result < 0) {
            stream->error = 1;
        }
        return result;
    }
}

int vfdprintf(uint32_t fd, const char *format, va_list args) {
    return vdprintf((int)fd, format, args);
}

int fdprintf(uint32_t fd, const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vdprintf((int)fd, format, args);
    va_end(args);
    return result;
}

int vprintf(const char *format, va_list args) {
    return vdprintf(STDOUT_FILENO, format, args);
}

int printf(const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vprintf(format, args);
    va_end(args);
    return result;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

static const char *scan_skip_space(const char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\n' ||
           *text == '\r' || *text == '\f' || *text == '\v') {
        text++;
    }
    return text;
}

int vsscanf(const char *text, const char *format, va_list args) {
    int assigned = 0;

    if (text == 0 || format == 0) {
        return -1;
    }
    while (*format != '\0') {
        int suppress = 0;
        int width = 0;

        if (*format == ' ' || *format == '\t' || *format == '\n' ||
            *format == '\r' || *format == '\f' || *format == '\v') {
            format = scan_skip_space(format);
            text = scan_skip_space(text);
            continue;
        }
        if (*format != '%') {
            if (*text != *format) {
                break;
            }
            text++;
            format++;
            continue;
        }
        format++;
        if (*format == '%') {
            if (*text != '%') {
                break;
            }
            text++;
            format++;
            continue;
        }
        if (*format == '*') {
            suppress = 1;
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format++ - '0');
        }
        switch (*format) {
            case 'd':
            case 'i':
            case 'o':
            case 'x':
            case 'X': {
                char *end = 0;
                int base = 10;
                long value;

                text = scan_skip_space(text);
                if (*format == 'i') {
                    base = 0;
                } else if (*format == 'o') {
                    base = 8;
                } else if (*format == 'x' || *format == 'X') {
                    base = 16;
                }
                value = strtol(text, &end, base);
                if (end == text) {
                    return assigned;
                }
                if (!suppress) {
                    *va_arg(args, int *) = (int)value;
                    assigned++;
                }
                text = end;
                break;
            }
            case 's': {
                char *out = suppress ? 0 : va_arg(args, char *);
                int copied = 0;

                text = scan_skip_space(text);
                while (*text != '\0' && *text != ' ' && *text != '\t' &&
                       *text != '\n' && *text != '\r' &&
                       (width == 0 || copied < width)) {
                    if (out != 0) {
                        out[copied] = *text;
                    }
                    copied++;
                    text++;
                }
                if (copied == 0) {
                    return assigned;
                }
                if (out != 0) {
                    out[copied] = '\0';
                    assigned++;
                }
                break;
            }
            case '[': {
                char *out = suppress ? 0 : va_arg(args, char *);
                char end_char = ']';
                int copied = 0;
                int invert = 0;
                int stop_on_newline = 0;

                format++;
                if (*format == '^') {
                    invert = 1;
                    format++;
                }
                if (*format == '\n') {
                    stop_on_newline = 1;
                    format++;
                }
                while (*format != '\0' && *format != end_char) {
                    format++;
                }
                while (*text != '\0' && (width == 0 || copied < width)) {
                    int match = stop_on_newline ? (*text == '\n') : 0;

                    if ((match && invert) || (!match && !invert)) {
                        break;
                    }
                    if (out != 0) {
                        out[copied] = *text;
                    }
                    copied++;
                    text++;
                }
                if (copied == 0) {
                    return assigned;
                }
                if (out != 0) {
                    out[copied] = '\0';
                    assigned++;
                }
                break;
            }
            default:
                return assigned;
        }
        if (*format != '\0') {
            format++;
        }
    }
    return assigned;
}

int sscanf(const char *text, const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = vsscanf(text, format, args);
    va_end(args);
    return result;
}
