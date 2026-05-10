#include "kernel/internal/core/console_internal.h"
#include "hal/hal.h"

static const char digits[] = "0123456789ABCDEF";
static const uint8_t g_console_ansi_palette[8] = {0, 4, 2, 6, 1, 5, 3, 7};

static uint16_t console_height(const struct console *console) {
    return (uint16_t)(console->bottom_row - console->top_row + 1u);
}

static uint16_t console_text_width(void) {
    return hal_display_text_columns();
}

static uint16_t console_text_rows(void) {
    return hal_display_text_rows();
}

static uint32_t console_pack_cell(uint32_t codepoint, uint8_t color, uint32_t flags) {
    if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = 0xfffdu;
    }
    return ((uint32_t)color << HAL_DISPLAY_CELL_COLOR_SHIFT) |
           (flags & HAL_DISPLAY_CELL_FLAGS_MASK) |
           (codepoint & HAL_DISPLAY_CELL_CODEPOINT_MASK);
}

static uint32_t console_blank_cell_with_color(uint8_t color) {
    return console_pack_cell(' ', color, 0);
}

static uint8_t console_cell_is_wide(uint32_t cell) {
    return (cell & HAL_DISPLAY_CELL_WIDE) != 0u;
}

static uint8_t console_cell_is_cont(uint32_t cell) {
    return (cell & HAL_DISPLAY_CELL_CONT) != 0u;
}

static uint8_t console_codepoint_width(uint32_t codepoint) {
    if ((codepoint >= 0x0300u && codepoint <= 0x036fu) ||
        (codepoint >= 0x1ab0u && codepoint <= 0x1affu) ||
        (codepoint >= 0x1dc0u && codepoint <= 0x1dffu) ||
        (codepoint >= 0x20d0u && codepoint <= 0x20ffu) ||
        (codepoint >= 0xfe20u && codepoint <= 0xfe2fu)) {
        return 0;
    }
    if ((codepoint >= 0x1100u && codepoint <= 0x115fu) ||
        (codepoint >= 0x2329u && codepoint <= 0x232au) ||
        (codepoint >= 0x2e80u && codepoint <= 0xa4cfu) ||
        (codepoint >= 0xac00u && codepoint <= 0xd7a3u) ||
        (codepoint >= 0xf900u && codepoint <= 0xfaffu) ||
        (codepoint >= 0xfe10u && codepoint <= 0xfe19u) ||
        (codepoint >= 0xfe30u && codepoint <= 0xfe6fu) ||
        (codepoint >= 0xff00u && codepoint <= 0xff60u) ||
        (codepoint >= 0xffe0u && codepoint <= 0xffe6u) ||
        (codepoint >= 0x20000u && codepoint <= 0x3fffdu)) {
        return 2;
    }
    return 1;
}

static uint32_t console_live_top_line(const struct console *console) {
    uint16_t height = console_height(console);

    if (console->history_line_count <= height) {
        return console->history_base_line;
    }
    return console->history_base_line + console->history_line_count - height;
}

static int console_is_live_view(const struct console *console) {
    return console->view_top_line == console_live_top_line(console);
}

static uint32_t console_line_to_slot(uint32_t line) {
    return line % CONSOLE_SCROLLBACK_LINES;
}

static uint32_t console_blank_cell(const struct console *console) {
    return console_blank_cell_with_color(console->default_color);
}

static uint32_t console_history_cell(const struct console *console, uint32_t line, uint16_t col) {
    return console->history[console_line_to_slot(line)][col];
}

static void console_clear_history_line(struct console *console, uint32_t line, uint8_t color) {
    uint32_t cell = console_blank_cell_with_color(color);
    uint32_t slot = console_line_to_slot(line);

    for (uint16_t col = 0; col < console_text_width(); col++) {
        console->history[slot][col] = cell;
    }
}

static void console_set_history_cell(struct console *console,
                                     uint32_t line,
                                     uint16_t col,
                                     uint8_t color,
                                     uint32_t codepoint,
                                     uint32_t flags) {
    console->history[console_line_to_slot(line)][col] =
        console_pack_cell(codepoint, color, flags);
}

static int console_visible_row_for_line(const struct console *console, uint32_t line, uint16_t *row_out) {
    uint16_t height = console_height(console);

    if (line < console->view_top_line || line >= console->view_top_line + height) {
        return 0;
    }
    *row_out = (uint16_t)(console->top_row + (uint16_t)(line - console->view_top_line));
    return 1;
}

static void console_write_display_cell_if_visible(const struct console *console, uint32_t line, uint16_t col) {
    uint16_t row = 0;

    if (console_visible_row_for_line(console, line, &row)) {
        hal_display_write_cell(row, col, console_history_cell(console, line, col));
    }
}

static void console_clear_overlapping_cells(struct console *console,
                                            uint32_t line,
                                            uint16_t col,
                                            uint8_t cell_width,
                                            uint8_t color) {
    uint16_t text_width = console_text_width();
    uint16_t start = col;
    uint16_t end;
    uint32_t blank = console_blank_cell_with_color(color);

    if (text_width == 0u || col >= text_width) {
        return;
    }
    end = (uint16_t)(col + (cell_width != 0u ? cell_width - 1u : 0u));
    if (end >= text_width) {
        end = (uint16_t)(text_width - 1u);
    }
    if (start > 0u && console_cell_is_cont(console_history_cell(console, line, start))) {
        start--;
    }
    if (end + 1u < text_width && console_cell_is_wide(console_history_cell(console, line, end))) {
        end++;
    }
    for (uint16_t cur = start; cur <= end; cur++) {
        console->history[console_line_to_slot(line)][cur] = blank;
        console_write_display_cell_if_visible(console, line, cur);
    }
}

static void console_update_cursor_row(struct console *console) {
    uint16_t height = console_height(console);
    int32_t relative = 0;

    if (console->cursor_line >= console->view_top_line) {
        relative = (int32_t)(console->cursor_line - console->view_top_line);
    }
    if (relative < 0) {
        relative = 0;
    }
    if (relative >= (int32_t)height) {
        relative = (int32_t)height - 1;
    }
    console->cursor_row = (uint16_t)(console->top_row + (uint16_t)relative);
}

static void console_render_row(const struct console *console, uint16_t row) {
    uint32_t blank = console_blank_cell(console);
    uint32_t history_end = console->history_base_line + console->history_line_count;
    uint32_t line = console->view_top_line + (uint32_t)(row - console->top_row);

    for (uint16_t col = 0; col < console_text_width(); col++) {
        uint32_t cell = blank;

        if (line >= console->history_base_line && line < history_end) {
            cell = console_history_cell(console, line, col);
        }
        hal_display_write_cell(row, col, cell);
    }
}

static void console_render_view(const struct console *console) {
    for (uint16_t row = console->top_row; row <= console->bottom_row; row++) {
        console_render_row(console, row);
    }
}

static void console_sync_cursor(const struct console *console) {
    if (!console_is_live_view(console)) {
        hal_display_set_cursor(console->bottom_row, (uint16_t)(console_text_width() - 1u));
        return;
    }
    hal_display_set_cursor(console->cursor_row, console->cursor_col);
}

static void console_render_and_sync(const struct console *console) {
    console_render_view(console);
    console_sync_cursor(console);
}

static void console_resume_live_view(struct console *console) {
    uint32_t live_top = console_live_top_line(console);

    if (console->view_top_line != live_top) {
        console->view_top_line = live_top;
        console_update_cursor_row(console);
        console_render_and_sync(console);
        return;
    }
    console_update_cursor_row(console);
}

static void console_append_history_line(struct console *console, uint8_t color) {
    uint32_t new_line = console->history_base_line + console->history_line_count;

    if (console->history_line_count == CONSOLE_SCROLLBACK_LINES) {
        console->history_base_line++;
        console->history_line_count--;
        if (console->view_top_line < console->history_base_line) {
            console->view_top_line = console->history_base_line;
        }
        if (console->cursor_line < console->history_base_line) {
            console->cursor_line = console->history_base_line;
        }
        new_line = console->history_base_line + console->history_line_count;
    }

    console_clear_history_line(console, new_line, color);
    console->history_line_count++;
}

static void console_scroll_live_display(struct console *console) {
    console_render_view(console);
}

static void console_newline(struct console *console) {
    uint32_t old_live_top = console_live_top_line(console);

    console->cursor_col = 0;
    if (console->cursor_line + 1u >= console->history_base_line + console->history_line_count) {
        console_append_history_line(console, console->default_color);
    }
    console->cursor_line++;

    if (console_is_live_view(console)) {
        uint32_t new_live_top = console_live_top_line(console);

        if (new_live_top != old_live_top) {
            console->view_top_line = new_live_top;
            console_update_cursor_row(console);
            console_scroll_live_display(console);
            console_sync_cursor(console);
            return;
        }
        console_update_cursor_row(console);
        console_sync_cursor(console);
        return;
    }

    console_resume_live_view(console);
}

static uint32_t console_row_to_line(const struct console *console, uint16_t row) {
    return console->view_top_line + (uint32_t)(row - console->top_row);
}

void console_init(struct console *console, uint16_t top_row, uint16_t bottom_row, uint8_t color) {
    uint16_t height;

    console->top_row = top_row;
    console->bottom_row = bottom_row;
    console->cursor_row = top_row;
    console->cursor_col = 0;
    console->default_color = color;
    console->history_base_line = 0;
    console->history_line_count = 0;
    console->cursor_line = 0;
    console->view_top_line = 0;

    height = console_height(console);
    for (uint16_t row = 0; row < height; row++) {
        console_clear_history_line(console, row, color);
        console->history_line_count++;
    }

    hal_display_enable_cursor(14, 15);
    console_render_and_sync(console);
}

void console_clear(struct console *console) {
    uint16_t height = console_height(console);

    console->history_base_line = 0;
    console->history_line_count = 0;
    console->cursor_line = 0;
    console->view_top_line = 0;
    console->cursor_row = console->top_row;
    console->cursor_col = 0;

    for (uint16_t row = 0; row < height; row++) {
        console_clear_history_line(console, row, console->default_color);
        console->history_line_count++;
    }

    console_render_and_sync(console);
}

void console_clear_row(struct console *console, uint16_t row, uint8_t color) {
    uint32_t line;

    console_resume_live_view(console);
    line = console_row_to_line(console, row);
    console_clear_history_line(console, line, color);
    console_render_row(console, row);
    console_sync_cursor(console);
}

void console_set_cursor(struct console *console, uint16_t row, uint16_t col) {
    console_resume_live_view(console);

    if (row < console->top_row) {
        row = console->top_row;
    }
    if (row > console->bottom_row) {
        row = console->bottom_row;
    }
    if (col >= console_text_width()) {
        col = (uint16_t)(console_text_width() - 1u);
    }

    console->cursor_row = row;
    console->cursor_col = col;
    console->cursor_line = console_row_to_line(console, row);
    console_sync_cursor(console);
}

uint16_t console_get_cursor_row(const struct console *console) {
    return console->cursor_row;
}

uint16_t console_get_cursor_col(const struct console *console) {
    return console->cursor_col;
}

void console_put_at(const struct console *console, uint16_t row, uint16_t col, char ch, uint8_t color) {
    struct console *mutable_console = (struct console *)console;
    uint32_t line;

    if (row < mutable_console->top_row || row > mutable_console->bottom_row || col >= console_text_width()) {
        return;
    }

    console_resume_live_view(mutable_console);
    line = console_row_to_line(mutable_console, row);
    console_clear_overlapping_cells(mutable_console, line, col, 1u, color);
    console_set_history_cell(mutable_console, line, col, color, (uint8_t)ch, 0);
    console_write_display_cell_if_visible(mutable_console, line, col);
    console_sync_cursor(mutable_console);
}

void console_putc(struct console *console, char ch, uint8_t color) {
    console_put_codepoint(console, (uint8_t)ch, color);
}

void console_put_codepoint(struct console *console, uint32_t codepoint, uint8_t color) {
    uint8_t cell_width;
    uint16_t text_width;

    console_resume_live_view(console);

    if (codepoint == '\n') {
        console_newline(console);
        return;
    }
    if (codepoint == '\r') {
        console->cursor_col = 0;
        console_sync_cursor(console);
        return;
    }
    if (codepoint == '\b') {
        if (console->cursor_col != 0) {
            console->cursor_col--;
        } else if (console->cursor_line > console->history_base_line) {
            console->cursor_line--;
            console->cursor_col = (uint16_t)(console_text_width() - 1u);
        }
        if (console->cursor_col > 0u &&
            console_cell_is_cont(console_history_cell(console, console->cursor_line, console->cursor_col))) {
            console->cursor_col--;
        }
        console_update_cursor_row(console);
        console_clear_overlapping_cells(console, console->cursor_line, console->cursor_col, 1u, color);
        console_sync_cursor(console);
        return;
    }
    if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = 0xfffdu;
    }

    cell_width = console_codepoint_width(codepoint);
    if (cell_width == 0u) {
        return;
    }

    text_width = console_text_width();
    if (text_width == 0u) {
        return;
    }
    if (cell_width > text_width) {
        codepoint = '?';
        cell_width = 1u;
    }
    if (console->cursor_col >= text_width ||
        console->cursor_col + (uint16_t)cell_width > text_width) {
        console_newline(console);
    }

    console_clear_overlapping_cells(console, console->cursor_line, console->cursor_col, cell_width, color);
    console_set_history_cell(console,
                             console->cursor_line,
                             console->cursor_col,
                             color,
                             codepoint,
                             cell_width == 2u ? HAL_DISPLAY_CELL_WIDE : 0u);
    console_write_display_cell_if_visible(console, console->cursor_line, console->cursor_col);
    if (cell_width == 2u) {
        console_set_history_cell(console,
                                 console->cursor_line,
                                 (uint16_t)(console->cursor_col + 1u),
                                 color,
                                 codepoint,
                                 HAL_DISPLAY_CELL_CONT);
        console_write_display_cell_if_visible(console, console->cursor_line, (uint16_t)(console->cursor_col + 1u));
    }
    console->cursor_col = (uint16_t)(console->cursor_col + cell_width);
    console_sync_cursor(console);
}

void console_write(struct console *console, const char *text, uint8_t color) {
    while (*text != '\0') {
        console_putc(console, *text++, color);
    }
}

void console_write_dec(struct console *console, uint32_t value, uint8_t color) {
    char buf[11];
    int pos = 0;

    if (value == 0) {
        console_putc(console, '0', color);
        return;
    }

    while (value > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (pos > 0) {
        console_putc(console, buf[--pos], color);
    }
}

void console_write_hex64(struct console *console, uint64_t value, uint8_t color) {
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(console, digits[(value >> shift) & 0x0f], color);
    }
}

void console_write_at(const struct console *console, uint16_t row, uint16_t col, const char *text, uint8_t color) {
    struct console *mutable_console = (struct console *)console;

    console_resume_live_view(mutable_console);
    while (*text != '\0' && col < console_text_width()) {
        console_put_at(mutable_console, row, col++, *text++, color);
    }
}

void console_write_dec_at(const struct console *console, uint16_t row, uint16_t col, uint32_t value, uint8_t color) {
    char buf[11];
    int pos = 0;

    if (value == 0) {
        console_put_at(console, row, col, '0', color);
        return;
    }
    while (value > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos > 0 && col < console_text_width()) {
        console_put_at(console, row, col++, buf[--pos], color);
    }
}

void console_write_hex32_at(const struct console *console, uint16_t row, uint16_t col, uint32_t value, uint8_t color) {
    for (int shift = 28; shift >= 0 && col < console_text_width(); shift -= 4) {
        console_put_at(console, row, col++, digits[(value >> shift) & 0x0f], color);
    }
}

void console_write_hex64_at(const struct console *console, uint16_t row, uint16_t col, uint64_t value, uint8_t color) {
    console_write_hex32_at(console, row, col, (uint32_t)(value >> 32), color);
    console_write_hex32_at(console, row, col + 8u, (uint32_t)value, color);
}

void console_scroll_page_up(struct console *console) {
    uint16_t height = console_height(console);
    uint32_t step = height > 1u ? (uint32_t)(height - 1u) : 1u;

    if (console->view_top_line <= console->history_base_line) {
        return;
    }
    if (console->view_top_line - console->history_base_line < step) {
        console->view_top_line = console->history_base_line;
    } else {
        console->view_top_line -= step;
    }
    console_render_and_sync(console);
}

void console_scroll_page_down(struct console *console) {
    uint16_t height = console_height(console);
    uint32_t step = height > 1u ? (uint32_t)(height - 1u) : 1u;
    uint32_t live_top = console_live_top_line(console);

    if (console->view_top_line >= live_top) {
        return;
    }
    if (live_top - console->view_top_line < step) {
        console->view_top_line = live_top;
    } else {
        console->view_top_line += step;
    }
    console_render_and_sync(console);
}

uint16_t console_width(void) {
    return console_text_width();
}

uint16_t console_rows(void) {
    return console_text_rows();
}

uint8_t console_ansi_palette_color(uint32_t index, int bright) {
    uint8_t color = g_console_ansi_palette[index & 7u];

    if (bright) {
        color = (uint8_t)(color + 8u);
    }
    return color;
}
