#pragma once

#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#include "kernel/public/input/keyboard_types.h"
#include "kernel/public/sys/system_limits.h"

enum {
    TTY_LINE_MAX = NOS_TTY_LINE_MAX
};

enum tty_read_mode {
    TTY_READ_LINE = 0,
    TTY_READ_CHAR = 1
};

enum {
    TTY_VIRTUAL_COUNT = 3
};

struct tty;

void tty_init(struct tty *tty, uint16_t top_row, uint16_t bottom_row, uint8_t color);
void tty_virtual_init_all(uint16_t top_row, uint16_t bottom_row, uint8_t color);
struct tty *tty_virtual(uint32_t index);
struct tty *tty_active(void);
uint32_t tty_active_index(void);
int tty_switch_active(uint32_t index);
void tty_set_foreground_pid(struct tty *tty, uint32_t pid);
uint32_t tty_foreground_pid(const struct tty *tty);
void tty_clear_foreground_pid(struct tty *tty, uint32_t pid);
void tty_clear(struct tty *tty);
void tty_putc(struct tty *tty, char ch, uint8_t color);
uint32_t tty_write(struct tty *tty, const char *data, uint32_t len, uint8_t color);
uint32_t tty_write_str(struct tty *tty, const char *text, uint8_t color);
void tty_write_dec(struct tty *tty, uint32_t value, uint8_t color);
void tty_write_hex64(struct tty *tty, uint64_t value, uint8_t color);
void tty_clear_row(struct tty *tty, uint16_t row, uint8_t color);
void tty_set_cursor(struct tty *tty, uint16_t row, uint16_t col);
uint16_t tty_cursor_row(const struct tty *tty);
void tty_put_at(struct tty *tty, uint16_t row, uint16_t col, char ch, uint8_t color);
void tty_show_prompt(struct tty *tty);
void tty_feed_key_event(struct tty *tty, const struct keyboard_event *event);
void tty_set_raw_input(struct tty *tty, int enabled);
int tty_has_line(const struct tty *tty);
uint32_t tty_read(struct tty *tty, char *out, uint32_t max_len, uint32_t mode);
