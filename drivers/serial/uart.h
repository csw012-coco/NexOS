#pragma once

#include <stdint.h>

enum {
    UART_PORT_COM1 = 0x3f8u
};

void uart_init(void);
int uart_is_ready(void);
void uart_enable_input(void);
void uart_poll_input(void);
void uart_set_console_input_enabled(int enabled);
int uart_pop_input_char(char *out);
int uart_pop_console_char(char *out);
uint32_t uart_read(char *buffer, uint32_t size);
uint32_t uart_read_tty(char *buffer, uint32_t size, int raw);
void uart_write_char(char ch);
void uart_write(const char *text);
uint32_t uart_write_buffer(const char *data, uint32_t size);
