#include "kernel/internal/core/tty_internal.h"
#include "kernel/internal/core/clipboard_internal.h"

static uint16_t tty_prompt_render_rows(uint16_t width, uint16_t origin_col, uint16_t span) {
    uint32_t cells = (uint32_t)origin_col + (uint32_t)span;

    if (width == 0u) {
        return 1u;
    }
    if (cells == 0u) {
        return 1u;
    }
    return (uint16_t)(((cells - 1u) / width) + 1u);
}

static uint16_t tty_input_cell_offset(const struct tty *tty,
                                      uint8_t byte_limit,
                                      uint16_t width,
                                      uint16_t origin_col) {
    uint16_t cells = 0u;
    uint32_t offset = 0u;

    while (offset < byte_limit && offset < tty->input_len) {
        uint32_t codepoint;
        uint32_t consumed = tty_utf8_decode_next(tty->input, tty->input_len, offset, &codepoint);
        uint16_t col;
        uint8_t cell_width;

        if (consumed == 0u || offset + consumed > byte_limit) {
            consumed = 1u;
            codepoint = (uint8_t)tty->input[offset];
        }
        col = width != 0u
                  ? (uint16_t)(((uint32_t)origin_col + cells) % width)
                  : 0u;
        if (codepoint == '\t') {
            cells = (uint16_t)(cells + TTY_TAB_WIDTH - (col % TTY_TAB_WIDTH));
            offset += consumed;
            continue;
        }
        cell_width = tty_codepoint_width(codepoint);
        if (width != 0u && cell_width > 1u && col + cell_width > width) {
            cells = (uint16_t)(cells + width - col);
        }
        cells = (uint16_t)(cells + cell_width);
        offset += consumed;
    }
    return cells;
}

void tty_render_prompt(struct tty *tty) {
    uint16_t width;
    uint16_t height;
    uint16_t origin_row;
    uint16_t origin_col;
    uint16_t previous_rows;
    uint16_t required_rows;
    uint16_t visible_rows;
    uint16_t i;

    if (!tty->input_origin_valid) {
        tty->input_origin_row = console_get_cursor_row(&tty->console);
        tty->input_origin_col = console_get_cursor_col(&tty->console);
        tty->input_origin_valid = 1u;
        tty->input_render_rows = 1u;
    }

    width = console_width();
    height = (uint16_t)(tty->console.bottom_row - tty->console.top_row + 1u);
    if (width == 0u || height == 0u) {
        return;
    }
    origin_row = tty->input_origin_row;
    origin_col = tty->input_origin_col;
    previous_rows = tty->input_render_rows != 0u ? tty->input_render_rows : 1u;
    required_rows = tty_prompt_render_rows(
        width,
        origin_col,
        tty_input_cell_offset(tty, tty->input_len, width, origin_col));
    visible_rows = required_rows;
    if ((uint32_t)(origin_row - tty->console.top_row) + visible_rows > height) {
        visible_rows = (uint16_t)(height - (origin_row - tty->console.top_row));
    }
    if (previous_rows > visible_rows) {
        visible_rows = previous_rows;
    }

    for (uint16_t row_offset = 0; row_offset < visible_rows; row_offset++) {
        uint16_t row = (uint16_t)(origin_row + row_offset);
        uint16_t start_col = row_offset == 0u ? origin_col : 0u;

        if (row > tty->console.bottom_row) {
            break;
        }
        for (uint16_t col = start_col; col < width; col++) {
            tty_put_at(tty, row, col, ' ', tty->console.default_color);
        }
    }

    console_set_cursor(&tty->console, origin_row, origin_col);
    for (i = 0; i < tty->input_len;) {
        uint32_t codepoint;
        uint32_t consumed = tty_utf8_decode_next(tty->input, tty->input_len, i, &codepoint);

        if (consumed == 0u) {
            break;
        }
        console_put_codepoint(&tty->console, codepoint, tty->text_color);
        i = (uint16_t)(i + consumed);
    }

    {
        uint32_t cursor_absolute =
            (uint32_t)origin_col +
            tty_input_cell_offset(tty, tty->input_cursor, width, origin_col);
        uint16_t cursor_row = (uint16_t)(origin_row + (uint16_t)(cursor_absolute / width));
        uint16_t cursor_col = (uint16_t)(cursor_absolute % width);

        if (cursor_row > tty->console.bottom_row) {
            cursor_row = tty->console.bottom_row;
            cursor_col = (uint16_t)(width - 1u);
        }
        tty_set_cursor(tty, cursor_row, cursor_col);
    }
    tty->input_render_rows = required_rows;
}

void tty_emit_ctrl_c_local(struct tty *tty) {
    if (tty == NULL) {
        return;
    }

    tty->input_len = 0;
    tty->input_cursor = 0;
    tty->input[0] = '\0';
    tty->ready_line[0] = '\0';
    tty->line_ready = 1;
    tty->input_origin_valid = 0u;
    tty->history_index = -1;
    tty->history_scratch_saved = 0u;
    tty_hangul_commit(tty);
    tty_write_str(tty, "^C\n", tty->text_color);
}

void tty_copy_selection(struct tty *tty) {
    if (tty == NULL) {
        return;
    }
    (void)kernel_clipboard_copy_console_selection(&tty->console);
}

void tty_insert_input_char(struct tty *tty, char ch) {
    if (ch == 0 || tty->input_len >= TTY_LINE_MAX) {
        return;
    }

    for (uint8_t i = tty->input_len; i > tty->input_cursor; i--) {
        tty->input[i] = tty->input[i - 1u];
    }
    tty->input[tty->input_cursor] = ch;
    tty->input_len++;
    tty->input_cursor++;
    tty->input[tty->input_len] = '\0';
}

void tty_insert_input_tab(struct tty *tty) {
    uint16_t width;
    uint16_t origin_col;
    uint16_t cursor_col;
    uint16_t spaces;

    if (tty == NULL || tty->input_len >= TTY_LINE_MAX) {
        return;
    }
    width = console_width();
    origin_col = tty->input_origin_valid
                     ? tty->input_origin_col
                     : console_get_cursor_col(&tty->console);
    cursor_col = width != 0u
                     ? (uint16_t)(((uint32_t)origin_col +
                                   tty_input_cell_offset(tty,
                                                         tty->input_cursor,
                                                         width,
                                                         origin_col)) %
                                  width)
                     : 0u;
    spaces = (uint16_t)(TTY_TAB_WIDTH - (cursor_col % TTY_TAB_WIDTH));
    while (spaces-- != 0u && tty->input_len < TTY_LINE_MAX) {
        tty_insert_input_char(tty, ' ');
    }
}

static void tty_set_input_line(struct tty *tty, const char *text) {
    uint16_t len = 0u;

    if (tty == NULL) {
        return;
    }
    while (text != NULL && text[len] != '\0' && len < TTY_LINE_MAX) {
        tty->input[len] = text[len];
        len++;
    }
    tty->input[len] = '\0';
    tty->input_len = (uint8_t)len;
    tty->input_cursor = (uint8_t)len;
    tty_hangul_commit(tty);
}

static int tty_text_equal(const char *left, const char *right) {
    uint16_t i = 0u;

    if (left == NULL || right == NULL) {
        return left == right;
    }
    while (left[i] != '\0' && left[i] == right[i]) {
        i++;
    }
    return left[i] == right[i];
}

void tty_history_store(struct tty *tty) {
    uint8_t slot;

    if (tty == NULL || tty->input_len == 0u) {
        return;
    }
    if (tty->history_len != 0u) {
        uint8_t previous = (uint8_t)((tty->history_next + TTY_HISTORY_MAX - 1u) % TTY_HISTORY_MAX);

        if (tty_text_equal(tty->history[previous], tty->input)) {
            return;
        }
    }
    slot = tty->history_next;
    for (uint16_t i = 0u; i <= tty->input_len; i++) {
        tty->history[slot][i] = tty->input[i];
    }
    tty->history_next = (uint8_t)((tty->history_next + 1u) % TTY_HISTORY_MAX);
    if (tty->history_len < TTY_HISTORY_MAX) {
        tty->history_len++;
    }
}

static void tty_history_load(struct tty *tty, uint8_t index) {
    uint8_t slot;

    if (tty == NULL || index >= tty->history_len) {
        return;
    }
    slot = (uint8_t)((tty->history_next + TTY_HISTORY_MAX - tty->history_len + index) % TTY_HISTORY_MAX);
    tty_set_input_line(tty, tty->history[slot]);
}

void tty_history_up(struct tty *tty) {
    if (tty == NULL || tty->history_len == 0u) {
        return;
    }
    tty_hangul_commit(tty);
    if (tty->history_index < 0) {
        for (uint16_t i = 0u; i <= tty->input_len; i++) {
            tty->history_scratch[i] = tty->input[i];
        }
        tty->history_scratch_saved = 1u;
        tty->history_index = (int8_t)(tty->history_len - 1u);
    } else if (tty->history_index > 0) {
        tty->history_index--;
    } else {
        return;
    }
    tty_history_load(tty, (uint8_t)tty->history_index);
}

void tty_history_down(struct tty *tty) {
    if (tty == NULL || tty->history_index < 0) {
        return;
    }
    tty_hangul_commit(tty);
    if ((uint8_t)(tty->history_index + 1) < tty->history_len) {
        tty->history_index++;
        tty_history_load(tty, (uint8_t)tty->history_index);
        return;
    }
    tty->history_index = -1;
    if (tty->history_scratch_saved) {
        tty_set_input_line(tty, tty->history_scratch);
    } else {
        tty_set_input_line(tty, "");
    }
}

void tty_delete_input_range(struct tty *tty, uint8_t start, uint8_t end) {
    uint8_t removed;

    if (tty == NULL || start >= end || end > tty->input_len) {
        return;
    }
    removed = (uint8_t)(end - start);
    for (uint16_t i = end; i <= tty->input_len; i++) {
        tty->input[i - removed] = tty->input[i];
    }
    tty->input_len = (uint8_t)(tty->input_len - removed);
    tty->input_cursor = start;
}

void tty_paste_text(struct tty *tty, const char *text, uint32_t len, int preserve_newlines) {
    if (tty == NULL || text == NULL || len == 0u) {
        return;
    }

    if (tty->raw_input) {
        for (uint32_t i = 0; i < len && text[i] != '\0'; i++) {
            char ch = text[i];

            if (tty->char_count >= TTY_CHAR_QUEUE_SIZE) {
                break;
            }
            if (!preserve_newlines) {
                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n' || ch == '\t') {
                    ch = ' ';
                }
            }
            tty_queue_char(tty, ch);
        }
        return;
    }

    for (uint32_t i = 0; i < len && text[i] != '\0' && tty->input_len < TTY_LINE_MAX; i++) {
        char ch = text[i];

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n' || ch == '\t') {
            ch = ' ';
        }
        tty_insert_input_char(tty, ch);
    }
    tty_render_prompt(tty);
}
