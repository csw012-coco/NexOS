#include "drivers/serial/uart.h"
#include "hal/hal.h"

enum {
    UART_REG_DATA = 0u,
    UART_REG_INTERRUPT_ENABLE = 1u,
    UART_REG_FIFO_CONTROL = 2u,
    UART_REG_LINE_CONTROL = 3u,
    UART_REG_MODEM_CONTROL = 4u,
    UART_REG_LINE_STATUS = 5u,
    UART_REG_SCRATCH = 7u,
    UART_LINE_STATUS_TX_EMPTY = 0x20u,
    UART_MODEM_DTR = 0x01u,
    UART_MODEM_RTS = 0x02u,
    UART_MODEM_OUT2 = 0x08u,
    UART_FIFO_ENABLE = 0x01u,
    UART_FIFO_CLEAR_RX = 0x02u,
    UART_FIFO_CLEAR_TX = 0x04u,
    UART_LINE_DLAB = 0x80u,
    UART_LINE_8N1 = 0x03u,
    UART_SCRATCH_TEST = 0x5au,
    UART_TX_SPIN_LIMIT = 100000u
};

static uint16_t g_uart_base = UART_PORT_COM1;
static int g_uart_ready = 0;

static uint8_t uart_read_reg(uint8_t reg) {
    return hal_io_in8((uint16_t)(g_uart_base + reg));
}

static void uart_write_reg(uint8_t reg, uint8_t value) {
    hal_io_out8((uint16_t)(g_uart_base + reg), value);
}

void uart_init(void) {
    uint8_t scratch;

    uart_write_reg(UART_REG_INTERRUPT_ENABLE, 0x00u);
    uart_write_reg(UART_REG_LINE_CONTROL, UART_LINE_DLAB);
    uart_write_reg(UART_REG_DATA, 0x01u);
    uart_write_reg(UART_REG_INTERRUPT_ENABLE, 0x00u);
    uart_write_reg(UART_REG_LINE_CONTROL, UART_LINE_8N1);
    uart_write_reg(UART_REG_FIFO_CONTROL,
                   UART_FIFO_ENABLE | UART_FIFO_CLEAR_RX | UART_FIFO_CLEAR_TX);
    uart_write_reg(UART_REG_MODEM_CONTROL, UART_MODEM_DTR | UART_MODEM_RTS | UART_MODEM_OUT2);

    uart_write_reg(UART_REG_SCRATCH, UART_SCRATCH_TEST);
    scratch = uart_read_reg(UART_REG_SCRATCH);
    g_uart_ready = scratch == UART_SCRATCH_TEST;
}

int uart_is_ready(void) {
    return g_uart_ready;
}

void uart_write_char(char ch) {
    uint32_t spin = 0;

    if (!g_uart_ready) {
        return;
    }
    while ((uart_read_reg(UART_REG_LINE_STATUS) & UART_LINE_STATUS_TX_EMPTY) == 0 &&
           spin < UART_TX_SPIN_LIMIT) {
        spin++;
    }
    if (spin >= UART_TX_SPIN_LIMIT) {
        return;
    }
    uart_write_reg(UART_REG_DATA, (uint8_t)ch);
}

void uart_write(const char *text) {
    if (text == 0 || !g_uart_ready) {
        return;
    }
    while (*text != '\0') {
        if (*text == '\n') {
            uart_write_char('\r');
        }
        uart_write_char(*text++);
    }
}
