#include "drivers/input/keyboard.h"
#include "hal/hal.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/public/core/tty.h"
#include "lib/string.h"

static int tty_selftest_inject_and_feed(uint8_t scancode) {
    struct keyboard_event event;

    if (!hal_keyboard_inject_scancode(scancode)) {
        return 0;
    }
    for (uint32_t ticks = 0; ticks < 100u; ticks++) {
        __asm__ volatile("sti; hlt" : : : "memory");
        if (input_services_pop_keyboard_event(&event)) {
            tty_feed_key_event(shared_services_active_tty(), &event);
            return 1;
        }
    }
    return 0;
}

int tty_selftest_input(void) {
    static const uint8_t scancodes[] = {
        0x1eu, 0x30u, 0x0eu, 0x2eu, 0x1cu
    };
    char line[TTY_LINE_MAX + 1u];

    tty_clear(shared_services_active_tty());
    input_services_prompt();
    for (uint32_t i = 0; i < sizeof(scancodes); i++) {
        if (!tty_selftest_inject_and_feed(scancodes[i])) {
            return 0;
        }
    }
    return tty_has_line(shared_services_active_tty()) &&
           tty_read(shared_services_active_tty(), line, sizeof(line), TTY_READ_LINE) != 0u &&
           streq(line, "ac");
}

static void tty_selftest_feed_key(enum keyboard_keycode keycode,
                                   char ascii,
                                   int ctrl,
                                   int shift) {
    struct keyboard_event event;

    memset(&event, 0, sizeof(event));
    event.keycode = keycode;
    event.ascii = ascii;
    event.pressed = 1u;
    event.shift = shift ? 1u : 0u;
    event.ctrl = ctrl ? 1u : 0u;
    tty_feed_key_event(shared_services_active_tty(), &event);
}

static int tty_selftest_read_expected(const char *expected) {
    char line[TTY_LINE_MAX + 1u];

    if (!tty_has_line(shared_services_active_tty()) ||
        tty_read(shared_services_active_tty(), line, sizeof(line), TTY_READ_LINE) == 0u) {
        return 0;
    }
    return streq(line, expected);
}

int tty_selftest_utf8_edit(void) {
    tty_clear(shared_services_active_tty());
    input_services_prompt();

    tty_selftest_feed_key(KEYBOARD_KEY_A, 'a', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_B, 'b', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_LEFT, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_V, 0, 1, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_END, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_ENTER, 0, 0, 0);
    if (!tty_selftest_read_expected("a b")) {
        return 0;
    }

    tty_selftest_feed_key(KEYBOARD_KEY_X, 'x', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_Y, 'y', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_A, 0, 1, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_Z, 'z', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_E, 0, 1, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_Q, 'q', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_ENTER, 0, 0, 0);
    if (!tty_selftest_read_expected("zxyq")) {
        return 0;
    }

    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_R, 'r', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_K, 'k', 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_ENTER, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    if (!tty_selftest_read_expected("\xea\xb0\x80")) {
        return 0;
    }

    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_D, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_L, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_LEFT_SHIFT, 0, 0, 1);
    tty_selftest_feed_key(KEYBOARD_KEY_T, 0, 0, 1);
    tty_selftest_feed_key(KEYBOARD_KEY_ENTER, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    if (!tty_selftest_read_expected("\xec\x9e\x88")) {
        return 0;
    }

    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_D, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_K, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_LEFT_SHIFT, 0, 0, 1);
    tty_selftest_feed_key(KEYBOARD_KEY_T, 0, 0, 1);
    tty_selftest_feed_key(KEYBOARD_KEY_D, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_M, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_F, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_ENTER, 0, 0, 0);
    tty_selftest_feed_key(KEYBOARD_KEY_HANGUL, 0, 0, 0);
    if (!tty_selftest_read_expected("\xec\x95\x98\xec\x9d\x84")) {
        return 0;
    }

    return 1;
}
