#pragma once

struct hal_early_ops {
    void (*console_init)(void);
    void (*console_clear)(void);
    void (*console_putc)(char ch);
    void (*halt)(void);
};

void hal_early_bind(const struct hal_early_ops *ops);
void hal_early_console_init(void);
void hal_early_console_clear(void);
void hal_early_console_putc(char ch);
void hal_early_halt(void);
