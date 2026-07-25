#include "kernel/internal/core/tty_internal.h"
#include "drivers/input/keyboard.h"

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

void tty_hangul_clear(struct tty *tty) {
    tty->hangul_initial = -1;
    tty->hangul_medial = -1;
    tty->hangul_final = -1;
    tty->hangul_bytes = 0u;
}

void tty_hangul_commit(struct tty *tty) {
    tty_hangul_clear(tty);
    tty->hangul_start = tty->input_cursor;
}

void tty_hangul_render_mode(const struct tty *tty) {
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

int tty_hangul_backspace(struct tty *tty) {
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

int tty_hangul_feed(struct tty *tty, const struct keyboard_event *event) {
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

void tty_raw_hangul_commit(struct tty *tty) {
    tty_hangul_clear(tty);
}

int tty_raw_hangul_backspace(struct tty *tty) {
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

int tty_raw_hangul_feed(struct tty *tty, const struct keyboard_event *event) {
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
