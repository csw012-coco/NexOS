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

static void buf_write_uint(char *dst,
                           uint32_t size,
                           uint32_t *pos,
                           uint64_t value,
                           uint32_t base,
                           int uppercase) {
    char tmp[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    uint32_t count = 0;

    if (base < 2u || base > 16u) {
        return;
    }
    if (value == 0) {
        buf_putc(dst, size, pos, '0');
        return;
    }
    while (value != 0) {
        tmp[count++] = digits[value % (uint64_t)base];
        value /= (uint64_t)base;
    }
    while (count != 0) {
        buf_putc(dst, size, pos, tmp[--count]);
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

        if (*fmt != '%') {
            buf_putc(dst, size, &pos, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            buf_putc(dst, size, &pos, *fmt++);
            continue;
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

                buf_write(dst, size, &pos, text);
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
                    buf_putc(dst, size, &pos, '-');
                    magnitude = (uint64_t)(-(value + 1)) + 1u;
                } else {
                    magnitude = (uint64_t)value;
                }
                buf_write_uint(dst, size, &pos, magnitude, 10u, 0);
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
                buf_write_uint(dst, size, &pos, value, 10u, 0);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t value;

                if (long_count >= 2) {
                    value = va_arg(ap, unsigned long long);
                } else if (long_count == 1) {
                    value = va_arg(ap, unsigned long);
                } else {
                    value = va_arg(ap, unsigned int);
                }
                buf_write_uint(dst, size, &pos, value, 16u, *fmt == 'X');
                break;
            }
            case 'p': {
                uint64_t value = (uint64_t)(uintptr_t)va_arg(ap, void *);

                buf_write(dst, size, &pos, "0x");
                buf_write_uint(dst, size, &pos, value, 16u, 0);
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
