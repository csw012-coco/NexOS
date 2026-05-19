#pragma once

#include <stdint.h>

struct console;

void console_init(struct console *console, uint16_t top_row, uint16_t bottom_row, uint8_t color);
void console_clear(struct console *console);
void console_clear_row(struct console *console, uint16_t row, uint8_t color);
void console_set_cursor(struct console *console, uint16_t row, uint16_t col);
uint16_t console_get_cursor_row(const struct console *console);
uint16_t console_get_cursor_col(const struct console *console);
void console_putc(struct console *console, char ch, uint8_t color);
void console_put_codepoint(struct console *console, uint32_t codepoint, uint8_t color);
void console_write(struct console *console, const char *text, uint8_t color);
void console_write_dec(struct console *console, uint32_t value, uint8_t color);
void console_write_hex64(struct console *console, uint64_t value, uint8_t color);
void console_write_at(const struct console *console, uint16_t row, uint16_t col, const char *text, uint8_t color);
void console_write_dec_at(const struct console *console, uint16_t row, uint16_t col, uint32_t value, uint8_t color);
void console_write_hex32_at(const struct console *console, uint16_t row, uint16_t col, uint32_t value, uint8_t color);
void console_write_hex64_at(const struct console *console, uint16_t row, uint16_t col, uint64_t value, uint8_t color);
void console_put_at(const struct console *console, uint16_t row, uint16_t col, char ch, uint8_t color);
void console_scroll_page_up(struct console *console);
void console_scroll_page_down(struct console *console);
void console_set_visible(struct console *console, int visible);
int console_is_visible(const struct console *console);
void console_mouse_select_begin(struct console *console, uint16_t row, uint16_t col);
void console_mouse_select_update(struct console *console, uint16_t row, uint16_t col);
void console_mouse_select_end(struct console *console, uint16_t row, uint16_t col);
void console_mouse_select_clear(struct console *console);
uint32_t console_get_selection_text(const struct console *console, char *out, uint32_t out_size);
uint16_t console_width(void);
uint16_t console_rows(void);
uint8_t console_ansi_palette_color(uint32_t index, int bright);
