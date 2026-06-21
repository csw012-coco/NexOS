#include "kernel/internal/core/tty_internal.h"
#include "drivers/input/keyboard.h"
#include "hal/hal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/public/core/profile.h"
#include "kernel/public/proc/job_control.h"

static struct tty g_virtual_ttys[TTY_VIRTUAL_COUNT];
static uint32_t g_active_tty_index;
static uint8_t g_virtual_ttys_ready;
static uint32_t g_tty_profile_write;

enum {
    TTY_ANSI_STATE_NONE = 0,
    TTY_ANSI_STATE_ESC = 1,
    TTY_ANSI_STATE_CSI = 2
};

#define TTY_TAB_WIDTH 8u

static uint8_t tty_codepoint_width(uint32_t codepoint) {
    return ((codepoint >= 0x1100u && codepoint <= 0x115fu) ||
            (codepoint >= 0x2e80u && codepoint <= 0xa4cfu) ||
            (codepoint >= 0xac00u && codepoint <= 0xd7a3u) ||
            (codepoint >= 0xf900u && codepoint <= 0xfaffu) ||
            (codepoint >= 0xff00u && codepoint <= 0xff60u)) ? 2u : 1u;
}

static uint8_t tty_utf8_encode(uint32_t codepoint, char out[4]) {
    if (codepoint <= 0x7fu) {
        out[0] = (char)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7ffu) {
        out[0] = (char)(0xc0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3fu));
        return 2u;
    }
    out[0] = (char)(0xe0u | (codepoint >> 12));
    out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
    out[2] = (char)(0x80u | (codepoint & 0x3fu));
    return 3u;
}

static uint32_t tty_hangul_codepoint(const struct tty *tty) {
    static const uint16_t initial_jamo[19] = {
        0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143,
        0x3145, 0x3146, 0x3147, 0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e
    };
    static const uint16_t medial_jamo[21] = {
        0x314f, 0x3150, 0x3151, 0x3152, 0x3153, 0x3154, 0x3155, 0x3156, 0x3157,
        0x3158, 0x3159, 0x315a, 0x315b, 0x315c, 0x315d, 0x315e, 0x315f, 0x3160,
        0x3161, 0x3162, 0x3163
    };

    if (tty->hangul_initial >= 0 && tty->hangul_medial >= 0) {
        uint32_t final = tty->hangul_final >= 0 ? (uint32_t)tty->hangul_final : 0u;
        return 0xac00u + ((uint32_t)tty->hangul_initial * 21u +
                          (uint32_t)tty->hangul_medial) * 28u + final;
    }
    if (tty->hangul_initial >= 0) {
        return initial_jamo[(uint8_t)tty->hangul_initial];
    }
    if (tty->hangul_medial >= 0) {
        return medial_jamo[(uint8_t)tty->hangul_medial];
    }
    return 0u;
}

static void tty_hangul_clear(struct tty *tty) {
    tty->hangul_initial = -1;
    tty->hangul_medial = -1;
    tty->hangul_final = -1;
    tty->hangul_bytes = 0u;
}

static void tty_hangul_commit(struct tty *tty) {
    tty_hangul_clear(tty);
    tty->hangul_start = tty->input_cursor;
}

static void tty_hangul_render_mode(const struct tty *tty) {
    uint16_t width = console_width();

    if (tty == NULL || width < 4u || !console_is_visible(&tty->console)) {
        return;
    }
    console_write_at(&tty->console,
                     tty->console.top_row,
                     (uint16_t)(width - 3u),
                     tty->hangul_mode ? "KO " : "EN ",
                     tty->text_color);
}

static int tty_replace_input_range(struct tty *tty,
                                   uint8_t start,
                                   uint8_t old_len,
                                   const char *text,
                                   uint8_t text_len) {
    int32_t delta = (int32_t)text_len - (int32_t)old_len;

    if (start > tty->input_len || start + old_len > tty->input_len ||
        (delta > 0 && tty->input_len + (uint32_t)delta > TTY_LINE_MAX)) {
        return 0;
    }
    if (delta > 0) {
        for (int32_t i = tty->input_len; i >= (int32_t)(start + old_len); i--) {
            tty->input[i + delta] = tty->input[i];
        }
    } else if (delta < 0) {
        for (uint32_t i = start + old_len; i <= tty->input_len; i++) {
            tty->input[i + delta] = tty->input[i];
        }
    }
    for (uint8_t i = 0; i < text_len; i++) {
        tty->input[start + i] = text[i];
    }
    tty->input_len = (uint8_t)((int32_t)tty->input_len + delta);
    tty->input_cursor = (uint8_t)(start + text_len);
    tty->input[tty->input_len] = '\0';
    return 1;
}

static int tty_hangul_update(struct tty *tty) {
    char encoded[4];
    uint8_t len = tty_utf8_encode(tty_hangul_codepoint(tty), encoded);

    if (tty->hangul_bytes == 0u) {
        tty->hangul_start = tty->input_cursor;
    }
    if (!tty_replace_input_range(tty, tty->hangul_start, tty->hangul_bytes, encoded, len)) {
        return 0;
    }
    tty->hangul_bytes = len;
    return 1;
}

static int8_t tty_hangul_combine_medial(int8_t a, int8_t b) {
    if (a == 8 && b == 0) return 9;
    if (a == 8 && b == 1) return 10;
    if (a == 8 && b == 20) return 11;
    if (a == 13 && b == 4) return 14;
    if (a == 13 && b == 5) return 15;
    if (a == 13 && b == 20) return 16;
    if (a == 18 && b == 20) return 19;
    return -1;
}

static int8_t tty_hangul_split_medial(int8_t value) {
    if (value >= 9 && value <= 11) return 8;
    if (value >= 14 && value <= 16) return 13;
    if (value == 19) return 18;
    return -1;
}

static int8_t tty_hangul_final_for_initial(int8_t initial) {
    static const int8_t map[19] = {1, 2, 4, 7, -1, 8, 16, 17, -1, 19, 20, 21, 22, -1, 23, 24, 25, 26, 27};
    return initial >= 0 && initial < 19 ? map[(uint8_t)initial] : -1;
}

static int8_t tty_hangul_initial_for_final(int8_t final) {
    static const int8_t map[28] = {-1, 0, 1, -1, 2, -1, -1, 3, 5, -1, -1, -1, -1, -1,
                                   -1, -1, 6, 7, -1, 9, 10, 11, 12, 14, 15, 16, 17, 18};
    return final >= 0 && final < 28 ? map[(uint8_t)final] : -1;
}

static int8_t tty_hangul_combine_final(int8_t a, int8_t b) {
    if (a == 1 && b == 19) return 3;
    if (a == 4 && b == 22) return 5;
    if (a == 4 && b == 27) return 6;
    if (a == 8 && b == 1) return 9;
    if (a == 8 && b == 16) return 10;
    if (a == 8 && b == 17) return 11;
    if (a == 8 && b == 19) return 12;
    if (a == 8 && b == 25) return 13;
    if (a == 8 && b == 26) return 14;
    if (a == 8 && b == 27) return 15;
    if (a == 17 && b == 19) return 18;
    return -1;
}

static int8_t tty_hangul_split_final(int8_t value, int8_t *second) {
    switch (value) {
        case 3: *second = 19; return 1;
        case 5: *second = 22; return 4;
        case 6: *second = 27; return 4;
        case 9: *second = 1; return 8;
        case 10: *second = 16; return 8;
        case 11: *second = 17; return 8;
        case 12: *second = 19; return 8;
        case 13: *second = 25; return 8;
        case 14: *second = 26; return 8;
        case 15: *second = 27; return 8;
        case 18: *second = 19; return 17;
        default: *second = value; return 0;
    }
}

static int tty_hangul_map_key(const struct keyboard_event *event, int8_t *value, int *vowel) {
    *vowel = 0;
    switch (event->keycode) {
        case KEYBOARD_KEY_R: *value = event->shift ? 1 : 0; return 1;
        case KEYBOARD_KEY_S: *value = 2; return 1;
        case KEYBOARD_KEY_E: *value = event->shift ? 4 : 3; return 1;
        case KEYBOARD_KEY_F: *value = 5; return 1;
        case KEYBOARD_KEY_A: *value = 6; return 1;
        case KEYBOARD_KEY_Q: *value = event->shift ? 8 : 7; return 1;
        case KEYBOARD_KEY_T: *value = event->shift ? 10 : 9; return 1;
        case KEYBOARD_KEY_D: *value = 11; return 1;
        case KEYBOARD_KEY_W: *value = event->shift ? 13 : 12; return 1;
        case KEYBOARD_KEY_C: *value = 14; return 1;
        case KEYBOARD_KEY_Z: *value = 15; return 1;
        case KEYBOARD_KEY_X: *value = 16; return 1;
        case KEYBOARD_KEY_V: *value = 17; return 1;
        case KEYBOARD_KEY_G: *value = 18; return 1;
        case KEYBOARD_KEY_K: *value = 0; *vowel = 1; return 1;
        case KEYBOARD_KEY_O: *value = event->shift ? 3 : 1; *vowel = 1; return 1;
        case KEYBOARD_KEY_I: *value = 2; *vowel = 1; return 1;
        case KEYBOARD_KEY_J: *value = 4; *vowel = 1; return 1;
        case KEYBOARD_KEY_P: *value = event->shift ? 7 : 5; *vowel = 1; return 1;
        case KEYBOARD_KEY_U: *value = 6; *vowel = 1; return 1;
        case KEYBOARD_KEY_H: *value = 8; *vowel = 1; return 1;
        case KEYBOARD_KEY_Y: *value = 12; *vowel = 1; return 1;
        case KEYBOARD_KEY_N: *value = 13; *vowel = 1; return 1;
        case KEYBOARD_KEY_B: *value = 17; *vowel = 1; return 1;
        case KEYBOARD_KEY_M: *value = 18; *vowel = 1; return 1;
        case KEYBOARD_KEY_L: *value = 20; *vowel = 1; return 1;
        default: return 0;
    }
}

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

static uint8_t tty_utf8_expected_length(uint8_t first) {
    if (first < 0x80u) {
        return 1u;
    }
    if ((first & 0xe0u) == 0xc0u) {
        return 2u;
    }
    if ((first & 0xf0u) == 0xe0u) {
        return 3u;
    }
    if ((first & 0xf8u) == 0xf0u) {
        return 4u;
    }
    return 0u;
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

static uint8_t tty_utf8_previous_offset(const char *data, uint8_t offset) {
    uint8_t previous;

    if (data == NULL || offset == 0u) {
        return 0u;
    }
    previous = (uint8_t)(offset - 1u);
    while (previous > 0u && tty_utf8_is_continuation((uint8_t)data[previous])) {
        previous--;
    }
    return previous;
}

static uint8_t tty_utf8_next_offset(const char *data, uint8_t len, uint8_t offset) {
    uint32_t codepoint;
    uint32_t consumed;

    if (data == NULL || offset >= len) {
        return len;
    }
    consumed = tty_utf8_decode_next(data, len, offset, &codepoint);
    if (consumed == 0u || consumed > (uint32_t)(len - offset)) {
        consumed = 1u;
    }
    return (uint8_t)(offset + consumed);
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

static void tty_raw_hangul_update(struct tty *tty) {
    uint32_t codepoint = tty_hangul_codepoint(tty);
    char encoded[4];
    uint8_t len;
    uint8_t needed;

    if (codepoint == 0u) {
        return;
    }
    len = tty_utf8_encode(codepoint, encoded);
    needed = (uint8_t)(len + (tty->hangul_bytes != 0u ? 1u : 0u));
    if (!tty_can_queue_chars(tty, needed)) {
        return;
    }
    if (tty->hangul_bytes != 0u) {
        tty_queue_char(tty, '\b');
    }
    for (uint8_t i = 0u; i < len; i++) {
        tty_queue_char(tty, encoded[i]);
    }
    tty->hangul_bytes = len;
}

static void tty_raw_hangul_commit(struct tty *tty) {
    tty_hangul_clear(tty);
}

static int tty_raw_hangul_backspace(struct tty *tty) {
    int8_t split;

    if (tty->hangul_bytes == 0u) {
        return 0;
    }
    if (tty->hangul_final > 0) {
        int8_t remaining;

        split = 0;
        remaining = tty_hangul_split_final(tty->hangul_final, &split);
        tty->hangul_final = remaining != 0 ? remaining : -1;
    } else if (tty->hangul_medial >= 0) {
        tty->hangul_medial = tty_hangul_split_medial(tty->hangul_medial);
    } else {
        tty_queue_char(tty, '\b');
        tty_hangul_clear(tty);
        return 1;
    }
    if (tty->hangul_initial < 0 && tty->hangul_medial < 0) {
        tty_queue_char(tty, '\b');
        tty_hangul_clear(tty);
    } else {
        tty_raw_hangul_update(tty);
    }
    return 1;
}

static int tty_raw_hangul_feed(struct tty *tty, const struct keyboard_event *event) {
    int8_t value;
    int vowel;

    if (!tty_hangul_map_key(event, &value, &vowel)) {
        tty_raw_hangul_commit(tty);
        return 0;
    }
    if (vowel) {
        if (tty->hangul_medial < 0) {
            tty->hangul_medial = value;
        } else if (tty->hangul_final > 0) {
            int8_t moved_final;
            int8_t remaining_final = tty_hangul_split_final(tty->hangul_final, &moved_final);
            int8_t next_initial = tty_hangul_initial_for_final(moved_final);

            tty->hangul_final = remaining_final > 0 ? remaining_final : -1;
            tty_raw_hangul_update(tty);
            tty_raw_hangul_commit(tty);
            tty->hangul_initial = next_initial;
            tty->hangul_medial = value;
        } else {
            int8_t combined = tty_hangul_combine_medial(tty->hangul_medial, value);

            if (combined >= 0) {
                tty->hangul_medial = combined;
            } else {
                tty_raw_hangul_commit(tty);
                tty->hangul_medial = value;
            }
        }
    } else {
        if (tty->hangul_initial < 0 && tty->hangul_medial < 0) {
            tty->hangul_initial = value;
        } else if (tty->hangul_medial < 0 || tty->hangul_initial < 0) {
            tty_raw_hangul_commit(tty);
            tty->hangul_initial = value;
        } else if (tty->hangul_final < 0) {
            int8_t final = tty_hangul_final_for_initial(value);

            if (final > 0) {
                tty->hangul_final = final;
            } else {
                tty_raw_hangul_commit(tty);
                tty->hangul_initial = value;
            }
        } else {
            int8_t second = tty_hangul_final_for_initial(value);
            int8_t combined = tty_hangul_combine_final(tty->hangul_final, second);

            if (combined > 0) {
                tty->hangul_final = combined;
            } else {
                tty_raw_hangul_commit(tty);
                tty->hangul_initial = value;
            }
        }
    }
    tty_raw_hangul_update(tty);
    return 1;
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

static void tty_clear_char_queue(struct tty *tty) {
    if (tty == NULL) {
        return;
    }
    tty->char_head = 0;
    tty->char_tail = 0;
    tty->char_count = 0;
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
    tty->history_index = -1;
    tty->history_scratch_saved = 0u;
    tty_hangul_commit(tty);
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

static void tty_insert_input_tab(struct tty *tty) {
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

static void tty_history_store(struct tty *tty) {
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

static void tty_history_up(struct tty *tty) {
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

static void tty_history_down(struct tty *tty) {
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

static void tty_delete_input_range(struct tty *tty, uint8_t start, uint8_t end) {
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
    tty->history_len = 0u;
    tty->history_next = 0u;
    tty->history_index = -1;
    tty->history_scratch_saved = 0u;
    tty->history_scratch[0] = '\0';
    tty->input_origin_row = top_row;
    tty->input_origin_col = 0;
    tty->input_render_rows = 1u;
    tty->input_origin_valid = 0;
    tty->prompt_cache_len = 0u;
    tty->hangul_mode = 0u;
    tty_hangul_clear(tty);
    tty->hangul_start = 0u;
    tty->output_utf8_len = 0u;
    tty->output_utf8_expected = 0u;
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
    hal_display_present();
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
    tty->prompt_cache_len = 0u;
}

void tty_putc(struct tty *tty, char ch, uint8_t color) {
    console_putc(&tty->console, ch, color);
}

uint32_t tty_write(struct tty *tty, const char *data, uint32_t len, uint8_t color) {
    uint32_t written = 0;
    uint64_t start;

    if (g_tty_profile_write == 0u) {
        g_tty_profile_write = kernel_profile_register("tty.write");
    }
    start = kernel_profile_clock();

    if (len != 0u) {
        /*
         * Output produced outside the active line editor should break the
         * current prompt anchor so the next prompt redraw starts from the
         * new cursor position instead of reusing stale coordinates.
         */
        tty->input_origin_valid = 0u;
        tty->input_render_rows = 1u;
    }
    while (written < len) {
        uint8_t ch = (uint8_t)data[written];
        uint16_t prompt_cache_before = tty->prompt_cache_len;

        if (ch == '\n' || ch == '\r') {
            tty->prompt_cache_len = 0u;
        } else if (tty->prompt_cache_len < TTY_PROMPT_CACHE_SIZE) {
            tty->prompt_cache[tty->prompt_cache_len++] = data[written];
        }
        if (tty->output_utf8_len != 0u) {
            if (!tty_utf8_is_continuation(ch)) {
                tty_write_parsed_codepoint(tty, 0xfffdu, color);
                tty->output_utf8_len = 0u;
                tty->output_utf8_expected = 0u;
                tty->prompt_cache_len = prompt_cache_before;
                continue;
            }
            tty->output_utf8[tty->output_utf8_len++] = (char)ch;
            written++;
            if (tty->output_utf8_len == tty->output_utf8_expected) {
                uint32_t codepoint = 0xfffdu;

                (void)tty_utf8_decode_next(tty->output_utf8,
                                           tty->output_utf8_len,
                                           0u,
                                           &codepoint);
                tty_write_parsed_codepoint(tty, codepoint, color);
                tty->output_utf8_len = 0u;
                tty->output_utf8_expected = 0u;
            }
        } else if (ch < 0x80u || tty->ansi_state != TTY_ANSI_STATE_NONE) {
            tty_write_parsed_char(tty, data[written], color);
            written++;
        } else {
            uint8_t expected = tty_utf8_expected_length(ch);

            if (expected == 0u) {
                tty_write_parsed_codepoint(tty, 0xfffdu, color);
                written++;
                continue;
            }
            tty->output_utf8[0] = (char)ch;
            tty->output_utf8_len = 1u;
            tty->output_utf8_expected = expected;
            written++;
        }
    }
    kernel_profile_record(g_tty_profile_write,
                          kernel_profile_clock() - start,
                          written);
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
    tty->history_index = -1;
    tty->history_scratch_saved = 0u;
    tty_hangul_commit(tty);
    tty_render_prompt(tty);
    hal_display_present();
}

void tty_set_raw_input(struct tty *tty, int enabled) {
    uint8_t raw_input = enabled ? 1u : 0u;

    if (tty == NULL) {
        return;
    }
    if (tty->raw_input != raw_input) {
        tty_hangul_clear(tty);
    }
    if (tty->raw_input != 0u && raw_input == 0u) {
        tty_clear_char_queue(tty);
    }
    tty->raw_input = raw_input;
}

static int tty_hangul_backspace(struct tty *tty) {
    int8_t split;

    if (tty->hangul_bytes == 0u || tty->input_cursor != tty->hangul_start + tty->hangul_bytes) {
        return 0;
    }
    if (tty->hangul_final > 0) {
        split = 0;
        if (tty_hangul_split_final(tty->hangul_final, &split) != 0) {
            tty->hangul_final = tty_hangul_split_final(tty->hangul_final, &split);
        } else {
            tty->hangul_final = -1;
        }
    } else if (tty->hangul_medial >= 0) {
        split = tty_hangul_split_medial(tty->hangul_medial);
        tty->hangul_medial = split;
    } else {
        (void)tty_replace_input_range(tty, tty->hangul_start, tty->hangul_bytes, "", 0u);
        tty_hangul_clear(tty);
        return 1;
    }
    if (tty->hangul_initial < 0 && tty->hangul_medial < 0) {
        (void)tty_replace_input_range(tty, tty->hangul_start, tty->hangul_bytes, "", 0u);
        tty_hangul_clear(tty);
    } else {
        (void)tty_hangul_update(tty);
    }
    return 1;
}

static int tty_hangul_feed(struct tty *tty, const struct keyboard_event *event) {
    int8_t value;
    int vowel;

    if (!tty_hangul_map_key(event, &value, &vowel)) {
        tty_hangul_commit(tty);
        return 0;
    }
    if (tty->input_cursor != tty->hangul_start + tty->hangul_bytes) {
        tty_hangul_commit(tty);
    }
    if (vowel) {
        if (tty->hangul_medial < 0) {
            tty->hangul_medial = value;
        } else if (tty->hangul_final > 0) {
            int8_t moved_final;
            int8_t remaining_final = tty_hangul_split_final(tty->hangul_final, &moved_final);
            int8_t next_initial = tty_hangul_initial_for_final(moved_final);

            tty->hangul_final = remaining_final > 0 ? remaining_final : -1;
            (void)tty_hangul_update(tty);
            tty_hangul_commit(tty);
            tty->hangul_initial = next_initial;
            tty->hangul_medial = value;
        } else {
            int8_t combined = tty_hangul_combine_medial(tty->hangul_medial, value);

            if (combined >= 0) {
                tty->hangul_medial = combined;
            } else {
                (void)tty_hangul_update(tty);
                tty_hangul_commit(tty);
                tty->hangul_medial = value;
            }
        }
    } else {
        if (tty->hangul_initial < 0 && tty->hangul_medial < 0) {
            tty->hangul_initial = value;
        } else if (tty->hangul_medial < 0) {
            (void)tty_hangul_update(tty);
            tty_hangul_commit(tty);
            tty->hangul_initial = value;
        } else if (tty->hangul_initial < 0) {
            (void)tty_hangul_update(tty);
            tty_hangul_commit(tty);
            tty->hangul_initial = value;
        } else if (tty->hangul_final < 0) {
            int8_t final = tty_hangul_final_for_initial(value);

            if (final > 0) {
                tty->hangul_final = final;
            } else {
                (void)tty_hangul_update(tty);
                tty_hangul_commit(tty);
                tty->hangul_initial = value;
            }
        } else {
            int8_t second = tty_hangul_final_for_initial(value);
            int8_t combined = tty_hangul_combine_final(tty->hangul_final, second);

            if (combined > 0) {
                tty->hangul_final = combined;
            } else {
                (void)tty_hangul_update(tty);
                tty_hangul_commit(tty);
                tty->hangul_initial = value;
            }
        }
    }
    (void)tty_hangul_update(tty);
    tty_render_prompt(tty);
    return 1;
}

void tty_feed_key_event(struct tty *tty, const struct keyboard_event *event) {
    char ch;
    int had_readable_input;

    if (tty == NULL || event == NULL || event->keycode == KEYBOARD_KEY_NONE || event->released) {
        return;
    }

    had_readable_input = tty->line_ready != 0 || tty->char_count > 0;
    if (event->keycode == KEYBOARD_KEY_RIGHT_ALT ||
        event->keycode == KEYBOARD_KEY_HANGUL ||
        event->keycode == KEYBOARD_KEY_F4 ||
        (event->ctrl && event->keycode == KEYBOARD_KEY_SPACE)) {
        tty_hangul_commit(tty);
        tty->hangul_mode ^= 1u;
        tty_hangul_render_mode(tty);
        return;
    }
    if (tty->raw_input) {
        if (event->keycode == KEYBOARD_KEY_BACKSPACE &&
            tty->hangul_mode &&
            tty_raw_hangul_backspace(tty)) {
            goto done;
        }
        if (tty->hangul_mode && !event->ctrl && tty_raw_hangul_feed(tty, event)) {
            goto done;
        }
        tty_raw_hangul_commit(tty);

        switch (event->keycode) {
            case KEYBOARD_KEY_ESC:
                tty_queue_char(tty, '\x1b');
                goto done;
            case KEYBOARD_KEY_TAB:
                tty_queue_char(tty, '\t');
                goto done;
            case KEYBOARD_KEY_PAGE_UP:
                console_scroll_page_up(&tty->console);
                goto done;
            case KEYBOARD_KEY_PAGE_DOWN:
                console_scroll_page_down(&tty->console);
                goto done;
            case KEYBOARD_KEY_UP:
                tty_queue_escape_bracket(tty, 'A');
                goto done;
            case KEYBOARD_KEY_DOWN:
                tty_queue_escape_bracket(tty, 'B');
                goto done;
            case KEYBOARD_KEY_RIGHT:
                tty_queue_escape_bracket(tty, 'C');
                goto done;
            case KEYBOARD_KEY_LEFT:
                tty_queue_escape_bracket(tty, 'D');
                goto done;
            case KEYBOARD_KEY_HOME:
                tty_queue_escape_bracket(tty, 'H');
                goto done;
            case KEYBOARD_KEY_END:
                tty_queue_escape_bracket(tty, 'F');
                goto done;
            case KEYBOARD_KEY_DELETE:
                tty_queue_escape_bracket_tilde(tty, '3');
                goto done;
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
        tty_hangul_commit(tty);
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
        if (event->keycode == KEYBOARD_KEY_L) {
            char prompt[TTY_PROMPT_CACHE_SIZE];
            uint16_t prompt_len = tty->prompt_cache_len;

            for (uint16_t i = 0u; i < prompt_len; i++) {
                prompt[i] = tty->prompt_cache[i];
            }
            tty_clear(tty);
            if (prompt_len != 0u) {
                tty_write(tty, prompt, prompt_len, tty->text_color);
            }
            tty_render_prompt(tty);
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_C) {
            tty_emit_ctrl_c_local(tty);
            goto done;
        }
    }

    if (event->keycode == KEYBOARD_KEY_ENTER) {
        tty_hangul_commit(tty);
        tty_history_store(tty);
        for (uint16_t i = 0; i <= tty->input_len; i++) {
            tty->ready_line[i] = tty->input[i];
        }
        tty->line_ready = 1;
        tty_putc(tty, '\n', tty->text_color);
        tty->input_len = 0;
        tty->input_cursor = 0;
        tty->input[0] = '\0';
        tty->input_origin_valid = 0u;
        tty->history_index = -1;
        tty->history_scratch_saved = 0u;
        goto done;
    }

    if (event->keycode == KEYBOARD_KEY_BACKSPACE) {
        uint8_t previous;

        if (tty_hangul_backspace(tty)) {
            tty_render_prompt(tty);
            goto done;
        }
        if (tty->input_len == 0 || tty->input_cursor == 0) {
            goto done;
        }
        previous = tty_utf8_previous_offset(tty->input, tty->input_cursor);
        tty_delete_input_range(tty, previous, tty->input_cursor);
        tty_render_prompt(tty);
        goto done;
    }

    switch (event->keycode) {
        case KEYBOARD_KEY_UP:
            tty_history_up(tty);
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_DOWN:
            tty_history_down(tty);
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_LEFT:
            tty_hangul_commit(tty);
            tty->input_cursor = tty_utf8_previous_offset(tty->input, tty->input_cursor);
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_RIGHT:
            tty_hangul_commit(tty);
            tty->input_cursor = tty_utf8_next_offset(tty->input, tty->input_len, tty->input_cursor);
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_HOME:
            tty_hangul_commit(tty);
            tty->input_cursor = 0u;
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_END:
            tty_hangul_commit(tty);
            tty->input_cursor = tty->input_len;
            tty_render_prompt(tty);
            goto done;
        case KEYBOARD_KEY_PAGE_UP:
            tty_hangul_commit(tty);
            console_scroll_page_up(&tty->console);
            goto done;
        case KEYBOARD_KEY_PAGE_DOWN:
            tty_hangul_commit(tty);
            console_scroll_page_down(&tty->console);
            goto done;
        case KEYBOARD_KEY_DELETE: {
            uint8_t next;

            tty_hangul_commit(tty);
            next = tty_utf8_next_offset(tty->input, tty->input_len, tty->input_cursor);
            tty_delete_input_range(tty, tty->input_cursor, next);
            tty_render_prompt(tty);
            goto done;
        }
        default:
            break;
    }

    if (tty->hangul_mode && !event->ctrl && tty_hangul_feed(tty, event)) {
        goto done;
    }
    tty_hangul_commit(tty);
    ch = event->ascii;
    if (ch == 0 || tty->input_len >= TTY_LINE_MAX) {
        goto done;
    }

    if (ch == '\t') {
        tty_insert_input_tab(tty);
    } else {
        tty_insert_input_char(tty, ch);
    }
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
