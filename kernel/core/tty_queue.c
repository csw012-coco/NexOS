#include "kernel/internal/core/tty_internal.h"

int tty_can_queue_chars(const struct tty *tty, uint8_t count) {
    return (uint16_t)tty->char_count + count <= TTY_CHAR_QUEUE_SIZE;
}

void tty_queue_char(struct tty *tty, char ch) {
    if (tty->char_count >= TTY_CHAR_QUEUE_SIZE) {
        return;
    }

    tty->char_queue[tty->char_tail] = ch;
    tty->char_tail = (uint8_t)((tty->char_tail + 1u) % TTY_CHAR_QUEUE_SIZE);
    tty->char_count++;
}

void tty_queue_escape_bracket(struct tty *tty, char suffix) {
    if (!tty_can_queue_chars(tty, 3u)) {
        return;
    }
    tty_queue_char(tty, '\x1b');
    tty_queue_char(tty, '[');
    tty_queue_char(tty, suffix);
}

void tty_queue_escape_bracket_tilde(struct tty *tty, char code) {
    if (!tty_can_queue_chars(tty, 4u)) {
        return;
    }
    tty_queue_char(tty, '\x1b');
    tty_queue_char(tty, '[');
    tty_queue_char(tty, code);
    tty_queue_char(tty, '~');
}

int tty_pop_char(struct tty *tty, char *out) {
    if (tty->char_count == 0) {
        return 0;
    }

    *out = tty->char_queue[tty->char_head];
    tty->char_head = (uint8_t)((tty->char_head + 1u) % TTY_CHAR_QUEUE_SIZE);
    tty->char_count--;
    return 1;
}

void tty_clear_char_queue(struct tty *tty) {
    if (tty == NULL) {
        return;
    }
    tty->char_head = 0;
    tty->char_tail = 0;
    tty->char_count = 0;
}
