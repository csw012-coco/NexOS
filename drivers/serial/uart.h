#pragma once

#include <stdint.h>

enum {
    UART_PORT_COM1 = 0x3f8u
};

void uart_init(void);
int uart_is_ready(void);
void uart_write_char(char ch);
void uart_write(const char *text);
