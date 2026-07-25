#pragma once

#include <stdint.h>
#include "kernel/internal/core/console_internal.h"
#include "kernel/public/core/tty.h"

enum {
    TTY_CHAR_QUEUE_SIZE = 128,
    TTY_ANSI_PARAM_MAX = 4,
    TTY_HISTORY_MAX = 8,
    TTY_PROMPT_CACHE_SIZE = 192,
    TTY_TAB_WIDTH = 8
};

enum {
    TTY_ANSI_STATE_NONE = 0,
    TTY_ANSI_STATE_ESC = 1,
    TTY_ANSI_STATE_CSI = 2
};

struct tty {
    struct console console;
    uint8_t text_color;
    uint8_t prompt_color;
    uint32_t foreground_pid;
    char input[TTY_LINE_MAX + 1];
    char ready_line[TTY_LINE_MAX + 1];
    char char_queue[TTY_CHAR_QUEUE_SIZE];
    char history[TTY_HISTORY_MAX][TTY_LINE_MAX + 1];
    char history_scratch[TTY_LINE_MAX + 1];
    char prompt_cache[TTY_PROMPT_CACHE_SIZE];
    uint8_t input_len;
    uint8_t input_cursor;
    uint8_t line_ready;
    uint8_t char_head;
    uint8_t char_tail;
    uint8_t char_count;
    uint8_t raw_input;
    uint8_t history_len;
    uint8_t history_next;
    int8_t history_index;
    uint8_t history_scratch_saved;
    uint8_t ansi_color;
    uint8_t ansi_active;
    uint8_t ansi_bold;
    uint8_t ansi_state;
    uint8_t ansi_param_count;
    uint8_t ansi_param_active;
    uint8_t ansi_private;
    uint16_t ansi_saved_row;
    uint16_t ansi_saved_col;
    uint16_t input_origin_row;
    uint16_t input_origin_col;
    uint16_t input_render_rows;
    uint16_t prompt_cache_len;
    uint8_t input_origin_valid;
    uint8_t hangul_mode;
    int8_t hangul_initial;
    int8_t hangul_medial;
    int8_t hangul_final;
    uint8_t hangul_start;
    uint8_t hangul_bytes;
    uint8_t output_utf8_len;
    uint8_t output_utf8_expected;
    char output_utf8[4];
    uint16_t ansi_params[TTY_ANSI_PARAM_MAX];
};

uint8_t tty_codepoint_width(uint32_t codepoint);
uint8_t tty_utf8_encode(uint32_t codepoint, char out[4]);
int tty_utf8_is_continuation(uint8_t ch);
uint8_t tty_utf8_expected_length(uint8_t first);
uint32_t tty_utf8_decode_next(const char *data, uint32_t len, uint32_t offset, uint32_t *codepoint);
uint8_t tty_utf8_previous_offset(const char *data, uint8_t offset);
uint8_t tty_utf8_next_offset(const char *data, uint8_t len, uint8_t offset);

void tty_ansi_reset_output(struct tty *tty);
void tty_write_parsed_char(struct tty *tty, char ch, uint8_t color);
void tty_write_parsed_codepoint(struct tty *tty, uint32_t codepoint, uint8_t color);

int tty_can_queue_chars(const struct tty *tty, uint8_t count);
void tty_queue_char(struct tty *tty, char ch);
void tty_queue_escape_bracket(struct tty *tty, char suffix);
void tty_queue_escape_bracket_tilde(struct tty *tty, char code);
int tty_pop_char(struct tty *tty, char *out);
void tty_clear_char_queue(struct tty *tty);

void tty_hangul_clear(struct tty *tty);
void tty_hangul_commit(struct tty *tty);
void tty_hangul_render_mode(const struct tty *tty);
void tty_raw_hangul_commit(struct tty *tty);
int tty_raw_hangul_backspace(struct tty *tty);
int tty_raw_hangul_feed(struct tty *tty, const struct keyboard_event *event);
int tty_hangul_backspace(struct tty *tty);
int tty_hangul_feed(struct tty *tty, const struct keyboard_event *event);

void tty_render_prompt(struct tty *tty);
void tty_emit_ctrl_c_local(struct tty *tty);
void tty_copy_selection(struct tty *tty);
void tty_insert_input_char(struct tty *tty, char ch);
void tty_insert_input_tab(struct tty *tty);
void tty_history_store(struct tty *tty);
void tty_history_up(struct tty *tty);
void tty_history_down(struct tty *tty);
void tty_delete_input_range(struct tty *tty, uint8_t start, uint8_t end);
void tty_paste_text(struct tty *tty, const char *text, uint32_t len, int preserve_newlines);
