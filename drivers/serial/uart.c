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
    UART_LINE_STATUS_DATA_READY = 0x01u,
    UART_LINE_STATUS_TX_EMPTY = 0x20u,
    UART_INTERRUPT_RX_AVAILABLE = 0x01u,
    UART_MODEM_DTR = 0x01u,
    UART_MODEM_RTS = 0x02u,
    UART_MODEM_OUT2 = 0x08u,
    UART_FIFO_ENABLE = 0x01u,
    UART_FIFO_CLEAR_RX = 0x02u,
    UART_FIFO_CLEAR_TX = 0x04u,
    UART_LINE_DLAB = 0x80u,
    UART_LINE_8N1 = 0x03u,
    UART_SCRATCH_TEST = 0x5au,
    UART_TX_SPIN_LIMIT = 100000u,
    UART_RX_QUEUE_SIZE = 256u,
    UART_TTY_LINE_SIZE = 256u
};

static uint16_t g_uart_base = UART_PORT_COM1;
static int g_uart_ready = 0;

struct uart_input_queue {
    uint8_t data[UART_RX_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
};

static struct uart_input_queue g_uart_rx_queue;
static struct uart_input_queue g_uart_console_queue;
static struct uart_input_queue g_uart_tty_line_queue;
static char g_uart_tty_input[UART_TTY_LINE_SIZE];
static uint32_t g_uart_tty_input_len;
static uint8_t g_uart_tty_echo_enabled = 1u;
static uint8_t g_uart_console_input_enabled = 1u;
static uint8_t g_uart_tty_raw_mode;

static uint8_t uart_read_reg(uint8_t reg) {
    return hal_io_in8((uint16_t)(g_uart_base + reg));
}

static void uart_write_reg(uint8_t reg, uint8_t value) {
    hal_io_out8((uint16_t)(g_uart_base + reg), value);
}

static void uart_queue_clear(struct uart_input_queue *queue) {
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
}

static void uart_queue_push(struct uart_input_queue *queue, uint8_t ch) {
    if (queue->count >= UART_RX_QUEUE_SIZE) {
        queue->tail = (queue->tail + 1u) % UART_RX_QUEUE_SIZE;
        queue->count--;
    }
    queue->data[queue->head] = ch;
    queue->head = (queue->head + 1u) % UART_RX_QUEUE_SIZE;
    queue->count++;
}

static int uart_queue_pop(struct uart_input_queue *queue, char *out) {
    uint8_t ch;

    if (out == 0 || queue->count == 0u) {
        return 0;
    }
    ch = queue->data[queue->tail];
    queue->tail = (queue->tail + 1u) % UART_RX_QUEUE_SIZE;
    queue->count--;
    *out = (char)ch;
    return 1;
}

static uint32_t uart_queue_read(struct uart_input_queue *queue, char *buffer, uint32_t size) {
    uint32_t read = 0;

    while (read < size && uart_queue_pop(queue, &buffer[read])) {
        read++;
    }
    return read;
}

static void uart_tty_echo_char(uint8_t ch) {
    if (g_uart_tty_echo_enabled == 0u) {
        return;
    }
    if (ch == '\n') {
        uart_write_char('\r');
    }
    uart_write_char((char)ch);
}

static void uart_tty_echo_backspace(void) {
    if (g_uart_tty_echo_enabled == 0u) {
        return;
    }
    uart_write_char('\b');
    uart_write_char(' ');
    uart_write_char('\b');
}

static void uart_tty_commit_input_line(void) {
    uint32_t i;

    for (i = 0; i < g_uart_tty_input_len; i++) {
        uart_queue_push(&g_uart_tty_line_queue, (uint8_t)g_uart_tty_input[i]);
    }
    uart_queue_push(&g_uart_tty_line_queue, (uint8_t)'\n');
    g_uart_tty_input_len = 0u;
    g_uart_tty_input[0] = '\0';
}

static void uart_tty_handle_input_char(uint8_t ch) {
    if (ch == '\r' || ch == '\n') {
        uart_tty_echo_char('\n');
        uart_tty_commit_input_line();
        return;
    }
    if (ch == '\b' || ch == 0x7fu) {
        if (g_uart_tty_input_len > 0u) {
            g_uart_tty_input_len--;
            g_uart_tty_input[g_uart_tty_input_len] = '\0';
            uart_tty_echo_backspace();
        }
        return;
    }
    if (ch == 0x03u) {
        g_uart_tty_input_len = 0u;
        g_uart_tty_input[0] = '\0';
        if (g_uart_tty_echo_enabled != 0u) {
            uart_write("^C\r\n");
        }
        uart_queue_push(&g_uart_tty_line_queue, ch);
        return;
    }
    if (g_uart_tty_input_len + 1u >= UART_TTY_LINE_SIZE) {
        return;
    }
    g_uart_tty_input[g_uart_tty_input_len] = (char)ch;
    g_uart_tty_input_len++;
    g_uart_tty_input[g_uart_tty_input_len] = '\0';
    uart_tty_echo_char(ch);
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
    uart_queue_clear(&g_uart_rx_queue);
    uart_queue_clear(&g_uart_console_queue);
    uart_queue_clear(&g_uart_tty_line_queue);
    g_uart_tty_input_len = 0u;
    g_uart_tty_input[0] = '\0';
    g_uart_tty_echo_enabled = 1u;
    g_uart_console_input_enabled = 1u;
    g_uart_tty_raw_mode = 0u;
}

int uart_is_ready(void) {
    return g_uart_ready;
}

static void uart_queue_input_char(uint8_t ch) {
    uart_queue_push(&g_uart_rx_queue, ch);
    if (g_uart_console_input_enabled != 0u) {
        uart_queue_push(&g_uart_console_queue, ch);
    }
    if (g_uart_tty_raw_mode == 0u) {
        uart_tty_handle_input_char(ch);
    }
}

void uart_set_console_input_enabled(int enabled) {
    g_uart_console_input_enabled = enabled != 0 ? 1u : 0u;
    if (g_uart_console_input_enabled == 0u) {
        uart_queue_clear(&g_uart_console_queue);
    }
}

void uart_enable_input(void) {
    if (!g_uart_ready) {
        return;
    }
    uart_write_reg(UART_REG_FIFO_CONTROL,
                   UART_FIFO_ENABLE | UART_FIFO_CLEAR_RX | UART_FIFO_CLEAR_TX);
    uart_write_reg(UART_REG_INTERRUPT_ENABLE, UART_INTERRUPT_RX_AVAILABLE);
    hal_irq_set_mask(4u, 0);
}

void uart_poll_input(void) {
    if (!g_uart_ready) {
        return;
    }
    for (uint32_t i = 0; i < UART_RX_QUEUE_SIZE; i++) {
        if ((uart_read_reg(UART_REG_LINE_STATUS) & UART_LINE_STATUS_DATA_READY) == 0u) {
            return;
        }
        uart_queue_input_char(uart_read_reg(UART_REG_DATA));
    }
}

int uart_pop_input_char(char *out) {
    if (out == 0) {
        return 0;
    }
    uart_poll_input();
    return uart_queue_pop(&g_uart_rx_queue, out);
}

int uart_pop_console_char(char *out) {
    if (out == 0) {
        return 0;
    }
    uart_poll_input();
    return uart_queue_pop(&g_uart_console_queue, out);
}

uint32_t uart_read(char *buffer, uint32_t size) {
    return uart_read_tty(buffer, size, 1);
}

uint32_t uart_read_tty(char *buffer, uint32_t size, int raw) {
    uint32_t read = 0;

    if (buffer == 0 || size == 0u) {
        return 0;
    }
    g_uart_tty_raw_mode = raw != 0 ? 1u : 0u;
    if (g_uart_tty_raw_mode != 0u) {
        g_uart_tty_input_len = 0u;
        g_uart_tty_input[0] = '\0';
    }
    uart_poll_input();
    if (raw) {
        return uart_queue_read(&g_uart_rx_queue, buffer, 1u);
    }
    while (read < size && uart_queue_pop(&g_uart_tty_line_queue, &buffer[read])) {
        read++;
        if (buffer[read - 1u] == '\n') {
            break;
        }
    }
    return read;
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

uint32_t uart_write_buffer(const char *data, uint32_t size) {
    uint32_t written = 0;

    if (data == 0 || !g_uart_ready) {
        return 0;
    }
    while (written < size) {
        if (data[written] == '\n') {
            uart_write_char('\r');
        }
        uart_write_char(data[written]);
        written++;
    }
    return written;
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
