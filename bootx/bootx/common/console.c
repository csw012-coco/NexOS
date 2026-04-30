#include "bootx.h"

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static uint16_t cursor_row;
static uint16_t cursor_col;
static uint8_t current_color = 0x00;

static void scroll_if_needed(void) {
    if (cursor_row < 25) {
        return;
    }
    for (uint16_t row = 1; row < 25; row++) {
        for (uint16_t col = 0; col < 80; col++) {
            vga[(row - 1) * 80 + col] = vga[row * 80 + col];
        }
    }
    for (uint16_t col = 0; col < 80; col++) {
        vga[24 * 80 + col] = (uint16_t)current_color << 8 | ' ';
    }
    cursor_row = 24;
}

void console_clear(void) {
    for (uint16_t i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)current_color << 8 | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
}

void console_set_color(uint8_t color) {
    current_color = color;
}

void console_set_cursor(uint16_t row, uint16_t col) {
    cursor_row = row;
    cursor_col = col;
}

void console_putc(char ch) {
    if (ch == '\n') {
        cursor_row++;
        cursor_col = 0;
        scroll_if_needed();
        return;
    }
    if (ch == '\r') {
        cursor_col = 0;
        return;
    }
    vga[cursor_row * 80 + cursor_col] = (uint16_t)current_color << 8 | (uint8_t)ch;
    cursor_col++;
    if (cursor_col >= 80) {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
    }
}

void console_puts(const char *str) {
    while (*str != '\0') {
        console_putc(*str++);
    }
}

void console_write_at(uint16_t row, uint16_t col, uint8_t color, const char *str) {
    while (*str != '\0' && row < 25 && col < 80) {
        vga[row * 80 + col] = (uint16_t)color << 8 | (uint8_t)*str++;
        col++;
    }
}

void console_write_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    console_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> shift) & 0xF]);
    }
}

void debug_puts(const char *str) {
    while (*str != '\0') {
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(*str));
        str++;
    }
}

void debug_put_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    debug_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        char ch = digits[(value >> shift) & 0xF];
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(ch));
    }
}

void halt_forever(void) {
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
