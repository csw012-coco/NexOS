#pragma once

#include <stdint.h>
#include "bootx/bootx.h"
#include "drivers/video/surface.h"

enum {
    FRAMEBUFFER_TEXT_MAX_COLUMNS = 240,
    FRAMEBUFFER_TEXT_MAX_ROWS = 80
};

void framebuffer_display_init(const struct bootx_console_info *console);
int framebuffer_display_enable_backbuffer(void);
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
uint32_t framebuffer_display_cell_height(void);
void framebuffer_display_bitblt(uint32_t src_x,
                                uint32_t src_y,
                                uint32_t width,
                                uint32_t height,
                                uint32_t dst_x,
                                uint32_t dst_y);
void framebuffer_display_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color);
void framebuffer_display_blit_surface(const struct surface *surface,
                                      uint32_t src_x,
                                      uint32_t src_y,
                                      uint32_t width,
                                      uint32_t height,
                                      int32_t dst_x,
                                      int32_t dst_y);
void framebuffer_display_draw_pixel(int32_t x, int32_t y, uint32_t rgb);
void framebuffer_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb);
void framebuffer_display_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void framebuffer_display_fill_rect_rgb(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void framebuffer_display_draw_triangle(int32_t x0,
                                       int32_t y0,
                                       int32_t x1,
                                       int32_t y1,
                                       int32_t x2,
                                       int32_t y2,
                                       uint32_t rgb);
void framebuffer_display_fill_triangle(int32_t x0,
                                       int32_t y0,
                                       int32_t x1,
                                       int32_t y1,
                                       int32_t x2,
                                       int32_t y2,
                                       uint32_t rgb);
void framebuffer_display_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void framebuffer_display_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void framebuffer_display_present(void);
void framebuffer_display_set_mouse_cursor_enabled(int enabled);
void framebuffer_display_move_mouse_cursor(int32_t dx, int32_t dy);
int framebuffer_display_mouse_cursor_cell(uint16_t *row_out, uint16_t *col_out);
