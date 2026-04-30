#pragma once

#include <stdint.h>
#include "kernel/internal/core/console_internal.h"
#include "kernel/public/core/tty.h"

enum {
    TTY_CHAR_QUEUE_SIZE = 128,
    TTY_ANSI_PARAM_MAX = 4
};

struct tty {
    struct console console;
    uint8_t text_color;
    uint8_t prompt_color;
    char input[TTY_LINE_MAX + 1];
    char ready_line[TTY_LINE_MAX + 1];
    char char_queue[TTY_CHAR_QUEUE_SIZE];
    uint8_t input_len;
    uint8_t input_cursor;
    uint8_t line_ready;
    uint8_t char_head;
    uint8_t char_tail;
    uint8_t char_count;
    uint8_t raw_input;
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
    uint8_t input_origin_valid;
    uint16_t ansi_params[TTY_ANSI_PARAM_MAX];
};
