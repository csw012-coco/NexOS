#include "kernel/internal/core/tty_internal.h"
#include "drivers/input/keyboard.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/public/proc/job_control.h"

static struct tty g_virtual_ttys[TTY_VIRTUAL_COUNT];
static uint32_t g_active_tty_index;
static uint8_t g_virtual_ttys_ready;

enum {
    TTY_ANSI_STATE_NONE = 0,
    TTY_ANSI_STATE_ESC = 1,
    TTY_ANSI_STATE_CSI = 2
};

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

static void tty_ansi_reset_output(struct tty *tty) {
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

static void tty_write_parsed_char(struct tty *tty, char ch, uint8_t color) {
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

static int tty_utf8_is_continuation(uint8_t ch) {
    return (ch & 0xc0u) == 0x80u;
}

static uint32_t tty_utf8_decode_next(const char *data, uint32_t len, uint32_t offset, uint32_t *codepoint) {
    uint8_t first;
    uint32_t needed = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;

    if (codepoint == 0 || offset >= len) {
        return 0;
    }

    first = (uint8_t)data[offset];
    if (first < 0x80u) {
        *codepoint = first;
        return 1;
    }
    if ((first & 0xe0u) == 0xc0u) {
        needed = 2;
        value = first & 0x1fu;
        minimum = 0x80u;
    } else if ((first & 0xf0u) == 0xe0u) {
        needed = 3;
        value = first & 0x0fu;
        minimum = 0x800u;
    } else if ((first & 0xf8u) == 0xf0u) {
        needed = 4;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        *codepoint = 0xfffdu;
        return 1;
    }

    if (offset + needed > len) {
        *codepoint = 0xfffdu;
        return 1;
    }
    for (uint32_t i = 1; i < needed; i++) {
        uint8_t next = (uint8_t)data[offset + i];

        if (next == 0 || !tty_utf8_is_continuation(next)) {
            *codepoint = 0xfffdu;
            return 1;
        }
        value = (value << 6) | (uint32_t)(next & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) {
        *codepoint = 0xfffdu;
        return 1;
    }

    *codepoint = value;
    return needed;
}

static void tty_write_parsed_codepoint(struct tty *tty, uint32_t codepoint, uint8_t color) {
    if (codepoint < 0x80u) {
        tty_write_parsed_char(tty, (char)codepoint, color);
        return;
    }
    if (tty->ansi_state != TTY_ANSI_STATE_NONE) {
        tty_ansi_reset_parser(tty);
    }
    console_put_codepoint(&tty->console, codepoint, tty_ansi_effective_color(tty, color));
}

static int tty_can_queue_chars(const struct tty *tty, uint8_t count) {
    return (uint16_t)tty->char_count + count <= TTY_CHAR_QUEUE_SIZE;
}

static void tty_queue_char(struct tty *tty, char ch) {
    if (tty->char_count >= TTY_CHAR_QUEUE_SIZE) {
        return;
    }

    tty->char_queue[tty->char_tail] = ch;
    tty->char_tail = (uint8_t)((tty->char_tail + 1u) % TTY_CHAR_QUEUE_SIZE);
    tty->char_count++;
}

static void tty_queue_escape_bracket(struct tty *tty, char suffix) {
    if (!tty_can_queue_chars(tty, 3u)) {
        return;
    }
    tty_queue_char(tty, '\x1b');
    tty_queue_char(tty, '[');
    tty_queue_char(tty, suffix);
}

static void tty_queue_escape_bracket_tilde(struct tty *tty, char code) {
    if (!tty_can_queue_chars(tty, 4u)) {
        return;
    }
    tty_queue_char(tty, '\x1b');
    tty_queue_char(tty, '[');
    tty_queue_char(tty, code);
    tty_queue_char(tty, '~');
}

static int tty_pop_char(struct tty *tty, char *out) {
    if (tty->char_count == 0) {
        return 0;
    }

    *out = tty->char_queue[tty->char_head];
    tty->char_head = (uint8_t)((tty->char_head + 1u) % TTY_CHAR_QUEUE_SIZE);
    tty->char_count--;
    return 1;
}

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

static void tty_render_prompt(struct tty *tty) {
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
    required_rows = tty_prompt_render_rows(width,
                                           origin_col,
                                           tty->input_len > tty->input_cursor ? tty->input_len : tty->input_cursor);
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

    for (i = 0; i < tty->input_len; i++) {
        uint32_t absolute = (uint32_t)origin_col + i;
        uint16_t row = (uint16_t)(origin_row + (uint16_t)(absolute / width));
        uint16_t col = (uint16_t)(absolute % width);

        if (row > tty->console.bottom_row) {
            break;
        }
        tty_put_at(tty, row, col, tty->input[i], tty->text_color);
    }

    {
        uint32_t cursor_absolute = (uint32_t)origin_col + tty->input_cursor;
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

static void tty_emit_ctrl_c_local(struct tty *tty) {
    if (tty == NULL) {
        return;
    }

    tty->input_len = 0;
    tty->input_cursor = 0;
    tty->input[0] = '\0';
    tty->ready_line[0] = '\0';
    tty->line_ready = 1;
    tty->input_origin_valid = 0u;
    tty_write_str(tty, "^C\n", tty->text_color);
}

static void tty_copy_selection(struct tty *tty) {
    if (tty == NULL) {
        return;
    }
    (void)kernel_clipboard_copy_console_selection(&tty->console);
}

static void tty_insert_input_char(struct tty *tty, char ch) {
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

static void tty_paste_text(struct tty *tty, const char *text, uint32_t len) {
    if (tty == NULL || text == NULL || len == 0u) {
        return;
    }

    if (tty->raw_input) {
        for (uint32_t i = 0; i < len && text[i] != '\0'; i++) {
            if (tty->char_count >= TTY_CHAR_QUEUE_SIZE) {
                break;
            }
            tty_queue_char(tty, text[i]);
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

void tty_init(struct tty *tty, uint16_t top_row, uint16_t bottom_row, uint8_t color) {
    tty->text_color = color;
    tty->prompt_color = color;
    tty->foreground_pid = 0;
    tty->input_len = 0;
    tty->input_cursor = 0;
    tty->input[0] = '\0';
    tty->ready_line[0] = '\0';
    tty->line_ready = 0;
    tty->char_head = 0;
    tty->char_tail = 0;
    tty->char_count = 0;
    tty->raw_input = 0;
    tty->input_origin_row = top_row;
    tty->input_origin_col = 0;
    tty->input_render_rows = 1u;
    tty->input_origin_valid = 0;
    tty->ansi_saved_row = top_row;
    tty->ansi_saved_col = 0;
    tty_ansi_reset_output(tty);
    console_init(&tty->console, top_row, bottom_row, color);
}

void tty_virtual_init_all(uint16_t top_row, uint16_t bottom_row, uint8_t color) {
    for (uint32_t i = 0; i < TTY_VIRTUAL_COUNT; i++) {
        tty_init(&g_virtual_ttys[i], top_row, bottom_row, color);
        console_set_visible(&g_virtual_ttys[i].console, i == 0u);
    }
    g_active_tty_index = 0u;
    g_virtual_ttys_ready = 1u;
    console_set_visible(&g_virtual_ttys[0].console, 1);
}

struct tty *tty_virtual(uint32_t index) {
    if (index >= TTY_VIRTUAL_COUNT) {
        return NULL;
    }
    return &g_virtual_ttys[index];
}

struct tty *tty_active(void) {
    return tty_virtual(g_active_tty_index);
}

uint32_t tty_active_index(void) {
    return g_active_tty_index;
}

int tty_switch_active(uint32_t index) {
    struct tty *old_tty;
    struct tty *new_tty;

    if (!g_virtual_ttys_ready || index >= TTY_VIRTUAL_COUNT) {
        return 0;
    }
    old_tty = tty_virtual(g_active_tty_index);
    new_tty = tty_virtual(index);
    if (old_tty == NULL || new_tty == NULL) {
        return 0;
    }
    if (index != g_active_tty_index) {
        console_set_visible(&old_tty->console, 0);
        g_active_tty_index = index;
    }
    console_set_visible(&new_tty->console, 1);
    return 1;
}

void tty_set_foreground_pid(struct tty *tty, uint32_t pid) {
    if (tty == NULL) {
        return;
    }
    tty->foreground_pid = pid;
}

uint32_t tty_foreground_pid(const struct tty *tty) {
    return tty != NULL ? tty->foreground_pid : 0u;
}

void tty_clear_foreground_pid(struct tty *tty, uint32_t pid) {
    if (tty == NULL || pid == 0u) {
        return;
    }
    if (tty->foreground_pid == pid) {
        tty->foreground_pid = 0u;
    }
}

void tty_clear(struct tty *tty) {
    console_clear(&tty->console);
    tty_ansi_reset_output(tty);
    tty->ansi_saved_row = tty->console.top_row;
    tty->ansi_saved_col = 0;
    tty->input_origin_valid = 0u;
    tty->input_render_rows = 1u;
}

void tty_putc(struct tty *tty, char ch, uint8_t color) {
    console_putc(&tty->console, ch, color);
}

uint32_t tty_write(struct tty *tty, const char *data, uint32_t len, uint8_t color) {
    uint32_t written = 0;

    if (len != 0u) {
        /*
         * Output produced outside the active line editor should break the
         * current prompt anchor so the next prompt redraw starts from the
         * new cursor position instead of reusing stale coordinates.
         */
        tty->input_origin_valid = 0u;
        tty->input_render_rows = 1u;
    }
    while (written < len && data[written] != '\0') {
        uint8_t ch = (uint8_t)data[written];

        if (ch < 0x80u || tty->ansi_state != TTY_ANSI_STATE_NONE) {
            tty_write_parsed_char(tty, data[written], color);
            written++;
        } else {
            uint32_t codepoint = 0xfffdu;
            uint32_t consumed = tty_utf8_decode_next(data, len, written, &codepoint);

            if (consumed == 0u) {
                break;
            }
            tty_write_parsed_codepoint(tty, codepoint, color);
            written += consumed;
        }
    }
    return written;
}

uint32_t tty_write_str(struct tty *tty, const char *text, uint8_t color) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return tty_write(tty, text, len, color);
}

void tty_write_dec(struct tty *tty, uint32_t value, uint8_t color) {
    console_write_dec(&tty->console, value, color);
}

void tty_write_hex64(struct tty *tty, uint64_t value, uint8_t color) {
    console_write_hex64(&tty->console, value, color);
}

void tty_clear_row(struct tty *tty, uint16_t row, uint8_t color) {
    console_clear_row(&tty->console, row, color);
}

void tty_set_cursor(struct tty *tty, uint16_t row, uint16_t col) {
    console_set_cursor(&tty->console, row, col);
}

uint16_t tty_cursor_row(const struct tty *tty) {
    return console_get_cursor_row(&tty->console);
}

void tty_put_at(struct tty *tty, uint16_t row, uint16_t col, char ch, uint8_t color) {
    console_put_at(&tty->console, row, col, ch, color);
}

void tty_show_prompt(struct tty *tty) {
    tty->input_len = 0;
    tty->input_cursor = 0;
    tty->input[0] = '\0';
    tty->input_origin_row = console_get_cursor_row(&tty->console);
    tty->input_origin_col = console_get_cursor_col(&tty->console);
    tty->input_render_rows = 1u;
    tty->input_origin_valid = 1u;
    tty_render_prompt(tty);
}

void tty_set_raw_input(struct tty *tty, int enabled) {
    tty->raw_input = enabled ? 1u : 0u;
}

void tty_feed_key_event(struct tty *tty, const struct keyboard_event *event) {
    char ch;
    int had_readable_input;

    if (tty == NULL || event == NULL || event->keycode == KEYBOARD_KEY_NONE || event->released) {
        return;
    }

    had_readable_input = tty->line_ready != 0 || tty->char_count > 0;
    if (tty->raw_input) {
        switch (event->keycode) {
            case KEYBOARD_KEY_ESC:
                tty_queue_char(tty, '\x1b');
                break;
            case KEYBOARD_KEY_TAB:
                tty_queue_char(tty, '\t');
                break;
            case KEYBOARD_KEY_PAGE_UP:
                console_scroll_page_up(&tty->console);
                break;
            case KEYBOARD_KEY_PAGE_DOWN:
                console_scroll_page_down(&tty->console);
                break;
            case KEYBOARD_KEY_UP:
                tty_queue_escape_bracket(tty, 'A');
                break;
            case KEYBOARD_KEY_DOWN:
                tty_queue_escape_bracket(tty, 'B');
                break;
            case KEYBOARD_KEY_RIGHT:
                tty_queue_escape_bracket(tty, 'C');
                break;
            case KEYBOARD_KEY_LEFT:
                tty_queue_escape_bracket(tty, 'D');
                break;
            case KEYBOARD_KEY_HOME:
                tty_queue_escape_bracket(tty, 'H');
                break;
            case KEYBOARD_KEY_END:
                tty_queue_escape_bracket(tty, 'F');
                break;
            case KEYBOARD_KEY_DELETE:
                tty_queue_escape_bracket_tilde(tty, '3');
                break;
            default:
                break;
        }

        if (event->ctrl) {
            if (event->shift && event->keycode == KEYBOARD_KEY_C) {
                tty_copy_selection(tty);
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_V) {
                tty_paste_text(tty, kernel_clipboard_text(), kernel_clipboard_size());
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_A) {
                tty_queue_char(tty, 0x01);
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_E) {
                tty_queue_char(tty, 0x05);
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_L) {
                tty_queue_char(tty, 0x0c);
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_C) {
                tty_queue_char(tty, 0x03);
                goto done;
            }
        }

        if (event->keycode == KEYBOARD_KEY_ENTER) {
            tty_queue_char(tty, '\n');
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_BACKSPACE) {
            tty_queue_char(tty, '\b');
            goto done;
        }

        ch = event->ascii;
        if (ch == 0) {
            goto done;
        }
        tty_queue_char(tty, ch);
        goto done;
    }

    if (event->ctrl) {
        if (event->shift && event->keycode == KEYBOARD_KEY_C) {
            tty_copy_selection(tty);
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_V) {
            tty_paste_text(tty, kernel_clipboard_text(), kernel_clipboard_size());
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_A) {
            tty->input_cursor = 0;
            tty_render_prompt(tty);
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_E) {
            tty->input_cursor = tty->input_len;
            tty_render_prompt(tty);
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_C) {
            tty_emit_ctrl_c_local(tty);
            goto done;
        }
    }

    if (event->keycode == KEYBOARD_KEY_ENTER) {
        for (uint8_t i = 0; i <= tty->input_len; i++) {
            tty->ready_line[i] = tty->input[i];
        }
        tty->line_ready = 1;
        tty_putc(tty, '\n', tty->text_color);
        tty->input_len = 0;
        tty->input_cursor = 0;
        tty->input[0] = '\0';
        tty->input_origin_valid = 0u;
        goto done;
    }

    if (event->keycode == KEYBOARD_KEY_BACKSPACE) {
        if (tty->input_len == 0 || tty->input_cursor == 0) {
            goto done;
        }
        for (uint8_t i = tty->input_cursor - 1u; i < tty->input_len; i++) {
            tty->input[i] = tty->input[i + 1u];
        }
        tty->input_cursor--;
        tty->input_len--;
        tty->input[tty->input_len] = '\0';
        tty_render_prompt(tty);
        goto done;
    }

    ch = event->ascii;
    if (ch == 0 || tty->input_len >= TTY_LINE_MAX) {
        goto done;
    }

    tty_insert_input_char(tty, ch);
    tty_render_prompt(tty);

done:
    if (!had_readable_input && (tty->line_ready != 0 || tty->char_count > 0)) {
        job_tty_wake_waiting_processes(tty);
    }
}

int tty_has_line(const struct tty *tty) {
    return tty->line_ready != 0;
}

uint32_t tty_read(struct tty *tty, char *out, uint32_t max_len, uint32_t mode) {
    uint32_t i = 0;
    char ch;

    if (max_len == 0) {
        return 0;
    }

    if (mode == TTY_READ_CHAR) {
        if (!tty_pop_char(tty, &ch)) {
            out[0] = '\0';
            return 0;
        }
        out[0] = ch;
        if (max_len > 1) {
            out[1] = '\0';
        }
        return 1;
    }

    if (!tty->line_ready) {
        out[0] = '\0';
        return 0;
    }

    if (tty->ready_line[0] == '\0') {
        out[0] = '\0';
        tty->line_ready = 0;
        return 1;
    }

    while (tty->ready_line[i] != '\0' && i + 1 < max_len) {
        out[i] = tty->ready_line[i];
        i++;
    }
    out[i] = '\0';
    tty->ready_line[0] = '\0';
    tty->line_ready = 0;
    return i;
}
