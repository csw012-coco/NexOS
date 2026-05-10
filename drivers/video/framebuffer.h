#pragma once

#include <stdint.h>
#include "bootx/bootx.h"

enum {
    FRAMEBUFFER_TEXT_MAX_COLUMNS = 240,
    FRAMEBUFFER_TEXT_MAX_ROWS = 80
};

void framebuffer_display_init(const struct bootx_console_info *console);
void framebuffer_display_load_font_from_boot_modules(const struct bootx_boot_info *boot_info);
int framebuffer_display_active(void);
uint32_t framebuffer_device_size(void);
int64_t framebuffer_device_read(uint32_t *offset_io, void *buffer, uint32_t size);
int64_t framebuffer_device_write(uint32_t *offset_io, const void *buffer, uint32_t size);
uint32_t framebuffer_display_read_cell(uint16_t row, uint16_t col);
void framebuffer_display_write_cell(uint16_t row, uint16_t col, uint32_t value);
void framebuffer_display_clear_row(uint16_t row, uint8_t color);
void framebuffer_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch);
void framebuffer_display_enable_cursor(uint8_t start, uint8_t end);
void framebuffer_display_set_cursor(uint16_t row, uint16_t col);
void framebuffer_display_tick(uint32_t ticks);
uint16_t framebuffer_display_columns(void);
uint16_t framebuffer_display_rows(void);
