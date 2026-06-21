#include "hal/early.h"

static const struct hal_early_ops *g_hal_early_ops;

void hal_early_bind(const struct hal_early_ops *ops) {
    g_hal_early_ops = ops;
}

void hal_early_console_init(void) {
    if (g_hal_early_ops != 0 && g_hal_early_ops->console_init != 0) {
        g_hal_early_ops->console_init();
    }
}

void hal_early_console_clear(void) {
    if (g_hal_early_ops != 0 && g_hal_early_ops->console_clear != 0) {
        g_hal_early_ops->console_clear();
    }
}

void hal_early_console_putc(char ch) {
    if (g_hal_early_ops != 0 && g_hal_early_ops->console_putc != 0) {
        g_hal_early_ops->console_putc(ch);
    }
}

void hal_early_halt(void) {
    if (g_hal_early_ops != 0 && g_hal_early_ops->halt != 0) {
        g_hal_early_ops->halt();
    }
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
