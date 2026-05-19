#pragma once

#include <stdint.h>
#include "hal/hal.h"
#include "kernel/public/core/console.h"

enum {
    CONSOLE_SCROLLBACK_LINES = 512
};

struct console {
    uint16_t top_row;
    uint16_t bottom_row;
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint8_t default_color;
    uint8_t visible;
    uint32_t history_base_line;
    uint32_t history_line_count;
    uint32_t cursor_line;
    uint32_t view_top_line;
    uint8_t selection_active;
    uint8_t selection_dragging;
    uint32_t selection_anchor_line;
    uint16_t selection_anchor_col;
    uint32_t selection_focus_line;
    uint16_t selection_focus_col;
    uint32_t history[CONSOLE_SCROLLBACK_LINES][HAL_TEXT_WIDTH];
};
