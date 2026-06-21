#include "hal/early.h"
#include "kernel/public/core/early_console.h"

void early_console_init(void) {
    hal_early_console_init();
}

void early_console_clear(void) {
    hal_early_console_clear();
}

void early_console_putc(char ch) {
    hal_early_console_putc(ch);
}

void early_console_write(const char *text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        early_console_putc(*text++);
    }
}

void early_console_write_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    early_console_write("0x");
    for (uint32_t shift = 28u; ; shift -= 4u) {
        early_console_putc(digits[(value >> shift) & 0x0fu]);
        if (shift == 0u) {
            break;
        }
    }
}
