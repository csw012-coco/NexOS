#pragma once

#include <stdint.h>

void early_console_init(void);
void early_console_clear(void);
void early_console_putc(char ch);
void early_console_write(const char *text);
void early_console_write_hex32(uint32_t value);
