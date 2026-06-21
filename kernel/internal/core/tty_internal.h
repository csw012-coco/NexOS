#pragma once

#include <stdint.h>
#include "kernel/internal/core/console_internal.h"
#include "kernel/public/core/tty.h"

enum {
    TTY_CHAR_QUEUE_SIZE = 128,
    TTY_ANSI_PARAM_MAX = 4,
    TTY_HISTORY_MAX = 8,
    TTY_PROMPT_CACHE_SIZE = 192
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
