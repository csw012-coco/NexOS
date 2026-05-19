#include "drivers/video/vga.h"
#include "hal/hal.h"

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static const char digits[] = "0123456789ABCDEF";

void vga_clear_screen(uint8_t color) {
    for (uint16_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = (uint16_t)color << 8 | ' ';
    }
}

void vga_clear_row(uint16_t row, uint8_t color) {
    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        vga[row * VGA_WIDTH + col] = (uint16_t)color << 8 | ' ';
    }
}

uint16_t vga_read_cell(uint16_t row, uint16_t col) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return 0;
    }
    return vga[row * VGA_WIDTH + col];
}

void vga_write_cell(uint16_t row, uint16_t col, uint16_t value) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return;
    }
    vga[row * VGA_WIDTH + col] = value;
}

void vga_put_at(uint16_t row, uint16_t col, uint8_t color, char ch) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return;
    }
    vga[row * VGA_WIDTH + col] = (uint16_t)color << 8 | (uint8_t)ch;
}

void vga_enable_cursor(uint8_t start, uint8_t end) {
    hal_io_out8(0x3d4, 0x0a);
    hal_io_out8(0x3d5, (uint8_t)((hal_io_in8(0x3d5) & 0xc0u) | start));

    hal_io_out8(0x3d4, 0x0b);
    hal_io_out8(0x3d5, (uint8_t)((hal_io_in8(0x3d5) & 0xe0u) | end));
}

void vga_set_cursor(uint16_t row, uint16_t col) {
    uint16_t pos;

    if (row >= VGA_HEIGHT) {
        row = VGA_HEIGHT - 1;
    }
    if (col >= VGA_WIDTH) {
        col = VGA_WIDTH - 1;
    }

    pos = (uint16_t)(row * VGA_WIDTH + col);
    hal_io_out8(0x3d4, 0x0f);
    hal_io_out8(0x3d5, (uint8_t)(pos & 0xffu));
    hal_io_out8(0x3d4, 0x0e);
    hal_io_out8(0x3d5, (uint8_t)((pos >> 8) & 0xffu));
}

void vga_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color) {
    if (top_row >= bottom_row || top_row >= VGA_HEIGHT) {
        return;
    }
    if (bottom_row >= VGA_HEIGHT) {
        bottom_row = VGA_HEIGHT - 1u;
    }
    for (uint16_t row = top_row; row < bottom_row; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            vga[row * VGA_WIDTH + col] = vga[(row + 1u) * VGA_WIDTH + col];
        }
    }
    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        vga[bottom_row * VGA_WIDTH + col] = (uint16_t)clear_color << 8 | ' ';
    }
}

void vga_write_line(uint16_t row, uint16_t col, uint8_t color, const char *text) {
    while (*text != '\0' && col < VGA_WIDTH) {
        vga_put_at(row, col++, color, *text++);
    }
}

void vga_write_hex32(uint16_t row, uint16_t col, uint8_t color, uint32_t value) {
    for (int shift = 28; shift >= 0 && col < VGA_WIDTH; shift -= 4) {
        vga_put_at(row, col++, color, digits[(value >> shift) & 0xf]);
    }
}

void vga_write_hex64(uint16_t row, uint16_t col, uint8_t color, uint64_t value) {
    vga_write_hex32(row, col, color, (uint32_t)(value >> 32));
    vga_write_hex32(row, col + 8, color, (uint32_t)value);
}

void vga_write_dec(uint16_t row, uint16_t col, uint8_t color, uint32_t value) {
    char buf[11];
    int pos = 0;

    if (value == 0) {
        vga_put_at(row, col, color, '0');
        return;
    }

    while (value > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (pos > 0 && col < VGA_WIDTH) {
        vga_put_at(row, col++, color, buf[--pos]);
    }
}
