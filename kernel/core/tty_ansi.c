#include "kernel/internal/core/tty_internal.h"

static uint8_t tty_ansi_effective_color(const struct tty *tty, uint8_t color) {
    if (tty->ansi_active && color == tty->text_color) {
        return tty->ansi_color;
    }
    return color;
}

static uint8_t tty_ansi_clear_color(const struct tty *tty) {
    return tty->ansi_active ? tty->ansi_color : tty->text_color;
}

static void tty_ansi_refresh_active(struct tty *tty) {
    tty->ansi_active = (tty->ansi_color != tty->text_color) || tty->ansi_bold != 0;
}

static void tty_ansi_reset_parser(struct tty *tty) {
    tty->ansi_state = TTY_ANSI_STATE_NONE;
    tty->ansi_param_count = 0;
    tty->ansi_param_active = 0;
    tty->ansi_private = 0;
    for (uint32_t i = 0; i < TTY_ANSI_PARAM_MAX; i++) {
        tty->ansi_params[i] = 0;
    }
}

void tty_ansi_reset_output(struct tty *tty) {
    tty->ansi_color = tty->text_color;
    tty->ansi_bold = 0;
    tty_ansi_refresh_active(tty);
    tty_ansi_reset_parser(tty);
}

static uint8_t tty_ansi_palette(uint32_t index, int bright) {
    return console_ansi_palette_color(index, bright);
}

static void tty_ansi_set_fg(struct tty *tty, uint8_t fg) {
    tty->ansi_color = (uint8_t)((tty->ansi_color & 0xf0u) | (fg & 0x0fu));
    tty_ansi_refresh_active(tty);
}

static void tty_ansi_set_bg(struct tty *tty, uint8_t bg) {
    tty->ansi_color = (uint8_t)((tty->ansi_color & 0x0fu) | ((bg & 0x0fu) << 4));
    tty_ansi_refresh_active(tty);
}

static void tty_ansi_apply_sgr(struct tty *tty, uint32_t param) {
    if (param == 0) {
        tty_ansi_reset_output(tty);
        return;
    }
    if (param == 1) {
        tty->ansi_bold = 1;
        if ((tty->ansi_color & 0x0fu) < 8u) {
            tty->ansi_color = (uint8_t)(tty->ansi_color + 8u);
        }
        tty_ansi_refresh_active(tty);
        return;
    }
    if (param == 22) {
        tty->ansi_bold = 0;
        if ((tty->ansi_color & 0x0fu) >= 8u) {
            tty->ansi_color = (uint8_t)(tty->ansi_color - 8u);
        }
        tty_ansi_refresh_active(tty);
        return;
    }
    if (param >= 30 && param <= 37) {
        tty_ansi_set_fg(tty, tty_ansi_palette(param - 30u, tty->ansi_bold != 0));
        return;
    }
    if (param == 39) {
        tty_ansi_set_fg(tty, tty->text_color & 0x0fu);
        return;
    }
    if (param >= 40 && param <= 47) {
        tty_ansi_set_bg(tty, tty_ansi_palette(param - 40u, 0));
        return;
    }
    if (param == 49) {
        tty_ansi_set_bg(tty, (tty->text_color >> 4) & 0x0fu);
        return;
    }
    if (param >= 90 && param <= 97) {
        tty_ansi_set_fg(tty, tty_ansi_palette(param - 90u, 1));
        return;
    }
    if (param >= 100 && param <= 107) {
        tty_ansi_set_bg(tty, tty_ansi_palette(param - 100u, 1));
    }
}

static uint32_t tty_ansi_param(const struct tty *tty, uint32_t index, uint32_t fallback) {
    if (index >= tty->ansi_param_count || tty->ansi_params[index] == 0) {
        return fallback;
    }
    return tty->ansi_params[index];
}

static void tty_ansi_fill_row(struct tty *tty, uint16_t row, uint16_t start_col, uint16_t end_col, uint8_t color) {
    uint16_t width = console_width();

    if (end_col >= width) {
        end_col = width - 1u;
    }
    if (start_col >= width || start_col > end_col) {
        return;
    }
    for (uint16_t col = start_col; col <= end_col; col++) {
        tty_put_at(tty, row, col, ' ', color);
    }
}

static void tty_ansi_clear_display(struct tty *tty, uint32_t mode) {
    uint16_t row = console_get_cursor_row(&tty->console);
    uint16_t col = console_get_cursor_col(&tty->console);
    uint8_t color = tty_ansi_clear_color(tty);

    if (mode == 2) {
        tty_clear(tty);
        return;
    }
    if (mode == 1) {
        for (uint16_t cur = tty->console.top_row; cur < row; cur++) {
            tty_clear_row(tty, cur, color);
        }
        tty_ansi_fill_row(tty, row, 0, col, color);
        tty_set_cursor(tty, row, col);
        return;
    }

    tty_ansi_fill_row(tty, row, col, console_width() - 1u, color);
    for (uint16_t cur = (uint16_t)(row + 1u); cur <= tty->console.bottom_row; cur++) {
        tty_clear_row(tty, cur, color);
    }
    tty_set_cursor(tty, row, col);
}

static void tty_ansi_clear_line(struct tty *tty, uint32_t mode) {
    uint16_t row = console_get_cursor_row(&tty->console);
    uint16_t col = console_get_cursor_col(&tty->console);
    uint8_t color = tty_ansi_clear_color(tty);

    if (mode == 2) {
        tty_clear_row(tty, row, color);
    } else if (mode == 1) {
        tty_ansi_fill_row(tty, row, 0, col, color);
    } else {
        tty_ansi_fill_row(tty, row, col, console_width() - 1u, color);
    }
    tty_set_cursor(tty, row, col);
}

static void tty_ansi_move_cursor(struct tty *tty, int32_t row_delta, int32_t col_delta) {
    int32_t row = (int32_t)console_get_cursor_row(&tty->console) + row_delta;
    int32_t col = (int32_t)console_get_cursor_col(&tty->console) + col_delta;
    int32_t width = (int32_t)console_width();

    if (row < (int32_t)tty->console.top_row) {
        row = (int32_t)tty->console.top_row;
    }
    if (row > (int32_t)tty->console.bottom_row) {
        row = (int32_t)tty->console.bottom_row;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= width) {
        col = width - 1;
    }
    tty_set_cursor(tty, (uint16_t)row, (uint16_t)col);
}

static void tty_ansi_apply_csi(struct tty *tty, char final) {
    switch (final) {
        case 'm':
            if (tty->ansi_param_count == 0) {
                tty_ansi_apply_sgr(tty, 0);
            } else {
                for (uint32_t i = 0; i < tty->ansi_param_count; i++) {
                    tty_ansi_apply_sgr(tty, tty->ansi_params[i]);
                }
            }
            break;
        case 'H':
        case 'f': {
            uint32_t ansi_row = tty_ansi_param(tty, 0, 1);
            uint32_t ansi_col = tty_ansi_param(tty, 1, 1);
            uint16_t row = (uint16_t)(tty->console.top_row + (ansi_row > 0 ? ansi_row - 1u : 0u));
            uint16_t col = (uint16_t)(ansi_col > 0 ? ansi_col - 1u : 0u);

            tty_set_cursor(tty, row, col);
            break;
        }
        case 'A':
            tty_ansi_move_cursor(tty, -(int32_t)tty_ansi_param(tty, 0, 1), 0);
            break;
        case 'B':
            tty_ansi_move_cursor(tty, (int32_t)tty_ansi_param(tty, 0, 1), 0);
            break;
        case 'C':
            tty_ansi_move_cursor(tty, 0, (int32_t)tty_ansi_param(tty, 0, 1));
            break;
        case 'D':
            tty_ansi_move_cursor(tty, 0, -(int32_t)tty_ansi_param(tty, 0, 1));
            break;
        case 'G':
            tty_set_cursor(tty,
                           console_get_cursor_row(&tty->console),
                           (uint16_t)(tty_ansi_param(tty, 0, 1) - 1u));
            break;
        case 'd':
            tty_set_cursor(tty,
                           (uint16_t)(tty->console.top_row + tty_ansi_param(tty, 0, 1) - 1u),
                           console_get_cursor_col(&tty->console));
            break;
        case 'J':
            tty_ansi_clear_display(tty, tty_ansi_param(tty, 0, 0));
            break;
        case 'K':
            tty_ansi_clear_line(tty, tty_ansi_param(tty, 0, 0));
            break;
        case 's':
            tty->ansi_saved_row = console_get_cursor_row(&tty->console);
            tty->ansi_saved_col = console_get_cursor_col(&tty->console);
            break;
        case 'u':
            tty_set_cursor(tty, tty->ansi_saved_row, tty->ansi_saved_col);
            break;
        default:
            break;
    }
}

static void tty_ansi_finish_param(struct tty *tty) {
    if (tty->ansi_param_active) {
        tty->ansi_param_count++;
        tty->ansi_param_active = 0;
    }
}

void tty_write_parsed_char(struct tty *tty, char ch, uint8_t color) {
retry:
    if (tty->ansi_state == TTY_ANSI_STATE_NONE) {
        if (ch == '\x1b') {
            tty->ansi_state = TTY_ANSI_STATE_ESC;
            return;
        }
        console_putc(&tty->console, ch, tty_ansi_effective_color(tty, color));
        return;
    }

    if (tty->ansi_state == TTY_ANSI_STATE_ESC) {
        if (ch == '[') {
            tty_ansi_reset_parser(tty);
            tty->ansi_state = TTY_ANSI_STATE_CSI;
            return;
        }

        tty->ansi_state = TTY_ANSI_STATE_NONE;
        console_putc(&tty->console, '\x1b', tty_ansi_effective_color(tty, color));
        goto retry;
    }

    if (ch == '?') {
        tty->ansi_private = 1u;
        return;
    }
    if (ch >= '0' && ch <= '9') {
        if (!tty->ansi_param_active) {
            if (tty->ansi_param_count >= TTY_ANSI_PARAM_MAX) {
                tty_ansi_reset_parser(tty);
                return;
            }
            tty->ansi_params[tty->ansi_param_count] = (uint16_t)(ch - '0');
            tty->ansi_param_active = 1u;
            return;
        }
        tty->ansi_params[tty->ansi_param_count] =
            (uint16_t)(tty->ansi_params[tty->ansi_param_count] * 10u + (uint16_t)(ch - '0'));
        return;
    }
    if (ch == ';') {
        if (!tty->ansi_param_active) {
            if (tty->ansi_param_count >= TTY_ANSI_PARAM_MAX) {
                tty_ansi_reset_parser(tty);
                return;
            }
            tty->ansi_params[tty->ansi_param_count++] = 0;
            return;
        }
        tty_ansi_finish_param(tty);
        return;
    }
    if (ch >= 0x40 && ch <= 0x7e) {
        tty_ansi_finish_param(tty);
        tty_ansi_apply_csi(tty, ch);
        tty_ansi_reset_parser(tty);
        return;
    }

    tty_ansi_reset_parser(tty);
}

void tty_write_parsed_codepoint(struct tty *tty, uint32_t codepoint, uint8_t color) {
    if (codepoint < 0x80u) {
        tty_write_parsed_char(tty, (char)codepoint, color);
        return;
    }
    if (tty->ansi_state != TTY_ANSI_STATE_NONE) {
        tty_ansi_reset_parser(tty);
    }
    console_put_codepoint(&tty->console, codepoint, tty_ansi_effective_color(tty, color));
}

