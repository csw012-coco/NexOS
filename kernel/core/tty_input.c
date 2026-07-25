#include "kernel/internal/core/tty_internal.h"
#include "drivers/input/keyboard.h"
#include "hal/hal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/public/proc/job_control.h"

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
            if (event->shift && event->keycode == KEYBOARD_KEY_V) {
                tty_paste_text(tty, kernel_clipboard_text(), kernel_clipboard_size(), 1);
                goto done;
            }
            if (event->keycode == KEYBOARD_KEY_V) {
                tty_queue_char(tty, 0x16);
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
        if (event->shift && event->keycode == KEYBOARD_KEY_V) {
            tty_paste_text(tty, kernel_clipboard_text(), kernel_clipboard_size(), 1);
            goto done;
        }
        if (event->keycode == KEYBOARD_KEY_V) {
            tty_insert_input_tab(tty);
            tty_render_prompt(tty);
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
