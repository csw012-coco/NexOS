#pragma once

#include <stdint.h>

enum {
    UART_PORT_COM1 = 0x3f8u
};

struct uart_status {
    uint32_t present;
    uint32_t ready;
    uint32_t input_enabled;
    uint32_t console_input_enabled;
    uint32_t tty_raw_mode;
    uint32_t base;
    uint32_t rx_pending;
    uint32_t console_pending;
    uint32_t tty_pending;
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t rx_dropped;
    uint32_t console_dropped;
    uint32_t tty_dropped;
    uint32_t tx_timeout_count;
    uint32_t line_error_count;
    uint32_t last_lsr;
    uint32_t last_error;
};

void uart_init(void);
int uart_is_ready(void);
void uart_enable_input(void);
void uart_poll_input(void);
void uart_set_console_input_enabled(int enabled);
int uart_query_status(struct uart_status *out);
int uart_pop_input_char(char *out);
int uart_pop_console_char(char *out);
uint32_t uart_read(char *buffer, uint32_t size);
uint32_t uart_read_tty(char *buffer, uint32_t size, int raw);
void uart_write_char(char ch);
void uart_write(const char *text);
uint32_t uart_write_buffer(const char *data, uint32_t size);
