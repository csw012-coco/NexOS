#include "user/libc/include/nlibc.h"
#include <stdarg.h>

static void buf_putc(char *dst, uint32_t size, uint32_t *pos, char ch) {
    if (dst != 0 && *pos + 1u < size) {
        dst[*pos] = ch;
    }
    (*pos)++;
}

static void buf_write(char *dst, uint32_t size, uint32_t *pos, const char *text) {
    uint32_t i = 0;

    if (text == 0) {
        text = "(null)";
    }
    while (text[i] != '\0') {
        buf_putc(dst, size, pos, text[i]);
        i++;
    }
}

static uint32_t buf_uint_digits(uint64_t value, uint32_t base) {
    uint32_t count = 1u;

    while (value >= base) {
        value /= base;
        count++;
    }
    return count;
}

static void buf_write_uint_formatted(char *dst,
                                     uint32_t size,
                                     uint32_t *pos,
                                     uint64_t value,
                                     uint32_t base,
                                     int uppercase,
                                     int negative,
                                     uint32_t width,
                                     int precision,
                                     int zero_pad) {
    char tmp[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    uint32_t count = buf_uint_digits(value, base);
    uint32_t zeroes = precision > (int)count ? (uint32_t)precision - count : 0u;
    uint32_t content = count + zeroes + (negative ? 1u : 0u);
    uint32_t spaces = width > content ? width - content : 0u;
    uint32_t out = count;

    if (base < 2u || base > 16u) {
        return;
    }
    if (zero_pad && precision < 0) {
        zeroes += spaces;
        spaces = 0u;
    }
    while (spaces-- != 0u) {
        buf_putc(dst, size, pos, ' ');
    }
    if (negative) {
        buf_putc(dst, size, pos, '-');
    }
    while (zeroes-- != 0u) {
        buf_putc(dst, size, pos, '0');
    }
    do {
        tmp[--out] = digits[value % (uint64_t)base];
        value /= (uint64_t)base;
    } while (out != 0u);
    for (uint32_t i = 0; i < count; i++) {
        buf_putc(dst, size, pos, tmp[i]);
    }
}

int vsnprintf(char *dst, uint32_t size, const char *fmt, va_list ap) {
    uint32_t pos = 0;

    if (fmt == 0) {
        if (dst != 0 && size != 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    while (*fmt != '\0') {
        int long_count = 0;
        int zero_pad = 0;
        uint32_t width = 0u;
        int precision = -1;

        if (*fmt != '%') {
            buf_putc(dst, size, &pos, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            buf_putc(dst, size, &pos, *fmt++);
            continue;
        }
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (uint32_t)(*fmt - '0');
            fmt++;
        }
        if (*fmt == '.') {
            precision = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        while (*fmt == 'l') {
            long_count++;
            fmt++;
        }
        switch (*fmt) {
            case 'c': {
                int ch = va_arg(ap, int);

                buf_putc(dst, size, &pos, (char)ch);
                break;
            }
            case 's': {
                const char *text = va_arg(ap, const char *);
                uint32_t length = 0u;

                if (text == 0) {
                    text = "(null)";
                }
                while (text[length] != '\0' &&
                       (precision < 0 || length < (uint32_t)precision)) {
                    length++;
                }
                while (width > length) {
                    buf_putc(dst, size, &pos, ' ');
                    width--;
                }
                for (uint32_t i = 0; i < length; i++) {
                    buf_putc(dst, size, &pos, text[i]);
                }
                break;
            }
            case 'd':
            case 'i': {
                int64_t value;
                uint64_t magnitude;

                if (long_count >= 2) {
                    value = va_arg(ap, long long);
                } else if (long_count == 1) {
                    value = va_arg(ap, long);
                } else {
                    value = va_arg(ap, int);
                }
                if (value < 0) {
                    magnitude = (uint64_t)(-(value + 1)) + 1u;
                } else {
                    magnitude = (uint64_t)value;
                }
                buf_write_uint_formatted(dst,
                                         size,
                                         &pos,
                                         magnitude,
                                         10u,
                                         0,
                                         value < 0,
                                         width,
                                         precision,
                                         zero_pad);
                break;
            }
            case 'u': {
                uint64_t value;

                if (long_count >= 2) {
                    value = va_arg(ap, unsigned long long);
                } else if (long_count == 1) {
                    value = va_arg(ap, unsigned long);
                } else {
                    value = va_arg(ap, unsigned int);
                }
                buf_write_uint_formatted(dst, size, &pos, value, 10u, 0, 0, width, precision, zero_pad);
                break;
            }
            case 'o':
            case 'x':
            case 'X': {
                uint64_t value;
                uint32_t base = *fmt == 'o' ? 8u : 16u;

                if (long_count >= 2) {
                    value = va_arg(ap, unsigned long long);
                } else if (long_count == 1) {
                    value = va_arg(ap, unsigned long);
                } else {
                    value = va_arg(ap, unsigned int);
                }
                buf_write_uint_formatted(dst,
                                         size,
                                         &pos,
                                         value,
                                         base,
                                         *fmt == 'X',
                                         0,
                                         width,
                                         precision,
                                         zero_pad);
                break;
            }
            case 'p': {
                uint64_t value = (uint64_t)(uintptr_t)va_arg(ap, void *);

                buf_write(dst, size, &pos, "0x");
                buf_write_uint_formatted(dst, size, &pos, value, 16u, 0, 0, width, precision, zero_pad);
                break;
            }
            default:
                buf_putc(dst, size, &pos, '%');
                if (*fmt != '\0') {
                    buf_putc(dst, size, &pos, *fmt);
                } else {
                    fmt--;
                }
                break;
        }
        if (*fmt != '\0') {
            fmt++;
        }
    }
    if (dst != 0 && size != 0) {
        uint32_t term = pos < size ? pos : (size - 1u);

        dst[term] = '\0';
    }
    return (int)pos;
}

int snprintf(char *dst, uint32_t size, const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return written;
}

int vdprintf(int fd, const char *fmt, va_list ap) {
    char buf[256];
    int written = vsnprintf(buf, sizeof(buf), fmt, ap);
    uint32_t len;

    if (written <= 0) {
        return written;
    }
    len = (uint32_t)written;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;
    }
    (void)write(fd, buf, len);
    return written;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = vdprintf(fd, fmt, ap);
    va_end(ap);
    return written;
}

int veprintf(const char *fmt, va_list ap) {
    return vdprintf(STDERR_FILENO, fmt, ap);
}

int eprintf(const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = veprintf(fmt, ap);
    va_end(ap);
    return written;
}

int vprintf(const char *fmt, va_list ap) {
    return vdprintf(STDOUT_FILENO, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = vprintf(fmt, ap);
    va_end(ap);
    return written;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    return stream != 0 ? vdprintf(stream->fd, fmt, ap) : -1;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = vfprintf(stream, fmt, ap);
    va_end(ap);
    return written;
}

int vfdprintf(uint32_t fd, const char *fmt, va_list ap) {
    return vdprintf((int)fd, fmt, ap);
}

int fdprintf(uint32_t fd, const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = vdprintf((int)fd, fmt, ap);
    va_end(ap);
    return written;
}
