#pragma once

#include <stdint.h>

enum {
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25
};

struct vga_status {
    uint32_t writes;
    uint32_t clears;
    uint32_t scrolls;
    uint32_t cursor_updates;
    uint32_t rejected_ops;
};

void vga_query_status(struct vga_status *out);
void vga_clear_screen(uint8_t color);
void vga_clear_row(uint16_t row, uint8_t color);
uint16_t vga_read_cell(uint16_t row, uint16_t col);
void vga_write_cell(uint16_t row, uint16_t col, uint16_t value);
void vga_write_line(uint16_t row, uint16_t col, uint8_t color, const char *text);
void vga_write_hex32(uint16_t row, uint16_t col, uint8_t color, uint32_t value);
void vga_write_hex64(uint16_t row, uint16_t col, uint8_t color, uint64_t value);
void vga_write_dec(uint16_t row, uint16_t col, uint8_t color, uint32_t value);
void vga_put_at(uint16_t row, uint16_t col, uint8_t color, char ch);
void vga_enable_cursor(uint8_t start, uint8_t end);
void vga_disable_cursor(void);
void vga_set_cursor(uint16_t row, uint16_t col);
void vga_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color);
