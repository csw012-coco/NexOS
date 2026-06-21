#include <stdarg.h>
#include <stdint.h>

#include "kernel/public/core/early_console.h"
#include "kernel/public/core/early_kprint.h"

static void early_kprint_unsigned(uint32_t value) {
    char digits[10];
    uint32_t count = 0;

    if (value == 0u) {
        early_console_putc('0');
        return;
    }
    while (value != 0u) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count != 0u) {
        early_console_putc(digits[--count]);
    }
}

void early_kprint(const char *format, ...) {
    va_list args;

    if (format == 0) {
        return;
    }
    va_start(args, format);
    while (*format != '\0') {
        if (*format != '%') {
            early_console_putc(*format++);
            continue;
        }
        format++;
        if (*format == '\0') {
            break;
        }
        if (*format == '%') {
            early_console_putc('%');
        } else if (*format == 'c') {
            early_console_putc((char)va_arg(args, int));
        } else if (*format == 's') {
            const char *text = va_arg(args, const char *);
            early_console_write(text != 0 ? text : "(null)");
        } else if (*format == 'u') {
            early_kprint_unsigned(va_arg(args, uint32_t));
        } else if (*format == 'x') {
            early_console_write_hex32(va_arg(args, uint32_t));
        } else {
            early_console_putc('%');
            early_console_putc(*format);
        }
        format++;
    }
    va_end(args);
}
