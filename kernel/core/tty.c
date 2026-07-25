#include "kernel/internal/core/tty_internal.h"
#include "hal/hal.h"
#include "kernel/public/core/profile.h"

static uint32_t g_tty_profile_write;

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
