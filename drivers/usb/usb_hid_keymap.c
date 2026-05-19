#include "drivers/usb/usb_hid_keymap.h"

enum keyboard_keycode usb_hid_usage_to_keycode(uint8_t usage) {
    static const enum keyboard_keycode usage_map[256] = {
        [0x04] = KEYBOARD_KEY_A, [0x05] = KEYBOARD_KEY_B, [0x06] = KEYBOARD_KEY_C,
        [0x07] = KEYBOARD_KEY_D, [0x08] = KEYBOARD_KEY_E, [0x09] = KEYBOARD_KEY_F,
        [0x0a] = KEYBOARD_KEY_G, [0x0b] = KEYBOARD_KEY_H, [0x0c] = KEYBOARD_KEY_I,
        [0x0d] = KEYBOARD_KEY_J, [0x0e] = KEYBOARD_KEY_K, [0x0f] = KEYBOARD_KEY_L,
        [0x10] = KEYBOARD_KEY_M, [0x11] = KEYBOARD_KEY_N, [0x12] = KEYBOARD_KEY_O,
        [0x13] = KEYBOARD_KEY_P, [0x14] = KEYBOARD_KEY_Q, [0x15] = KEYBOARD_KEY_R,
        [0x16] = KEYBOARD_KEY_S, [0x17] = KEYBOARD_KEY_T, [0x18] = KEYBOARD_KEY_U,
        [0x19] = KEYBOARD_KEY_V, [0x1a] = KEYBOARD_KEY_W, [0x1b] = KEYBOARD_KEY_X,
        [0x1c] = KEYBOARD_KEY_Y, [0x1d] = KEYBOARD_KEY_Z,
        [0x1e] = KEYBOARD_KEY_1, [0x1f] = KEYBOARD_KEY_2, [0x20] = KEYBOARD_KEY_3,
        [0x21] = KEYBOARD_KEY_4, [0x22] = KEYBOARD_KEY_5, [0x23] = KEYBOARD_KEY_6,
        [0x24] = KEYBOARD_KEY_7, [0x25] = KEYBOARD_KEY_8, [0x26] = KEYBOARD_KEY_9,
        [0x27] = KEYBOARD_KEY_0,
        [0x28] = KEYBOARD_KEY_ENTER,
        [0x29] = KEYBOARD_KEY_ESC,
        [0x2a] = KEYBOARD_KEY_BACKSPACE,
        [0x2b] = KEYBOARD_KEY_TAB,
        [0x2c] = KEYBOARD_KEY_SPACE,
        [0x2d] = KEYBOARD_KEY_MINUS,
        [0x2e] = KEYBOARD_KEY_EQUAL,
        [0x2f] = KEYBOARD_KEY_LEFT_BRACKET,
        [0x30] = KEYBOARD_KEY_RIGHT_BRACKET,
        [0x31] = KEYBOARD_KEY_BACKSLASH,
        [0x33] = KEYBOARD_KEY_SEMICOLON,
        [0x34] = KEYBOARD_KEY_APOSTROPHE,
        [0x35] = KEYBOARD_KEY_GRAVE,
        [0x36] = KEYBOARD_KEY_COMMA,
        [0x37] = KEYBOARD_KEY_PERIOD,
        [0x38] = KEYBOARD_KEY_SLASH,
        [0x39] = KEYBOARD_KEY_CAPS_LOCK,
        [0x3a] = KEYBOARD_KEY_F1,
        [0x3b] = KEYBOARD_KEY_F2,
        [0x3c] = KEYBOARD_KEY_F3,
        [0x47] = KEYBOARD_KEY_SCROLL_LOCK,
        [0x4a] = KEYBOARD_KEY_HOME,
        [0x4b] = KEYBOARD_KEY_PAGE_UP,
        [0x4c] = KEYBOARD_KEY_DELETE,
        [0x4d] = KEYBOARD_KEY_END,
        [0x4e] = KEYBOARD_KEY_PAGE_DOWN,
        [0x4f] = KEYBOARD_KEY_RIGHT,
        [0x50] = KEYBOARD_KEY_LEFT,
        [0x51] = KEYBOARD_KEY_DOWN,
        [0x52] = KEYBOARD_KEY_UP,
        [0x53] = KEYBOARD_KEY_NUM_LOCK,
        [0xe0] = KEYBOARD_KEY_LEFT_CTRL,
        [0xe1] = KEYBOARD_KEY_LEFT_SHIFT,
        [0xe2] = KEYBOARD_KEY_LEFT_ALT,
        [0xe4] = KEYBOARD_KEY_RIGHT_CTRL,
        [0xe5] = KEYBOARD_KEY_RIGHT_SHIFT,
        [0xe6] = KEYBOARD_KEY_RIGHT_ALT
    };

    return usage_map[usage];
}

int usb_hid_keycode_can_repeat(enum keyboard_keycode keycode) {
    switch (keycode) {
        case KEYBOARD_KEY_NONE:
        case KEYBOARD_KEY_LEFT_SHIFT:
        case KEYBOARD_KEY_RIGHT_SHIFT:
        case KEYBOARD_KEY_LEFT_CTRL:
        case KEYBOARD_KEY_RIGHT_CTRL:
        case KEYBOARD_KEY_LEFT_ALT:
        case KEYBOARD_KEY_RIGHT_ALT:
        case KEYBOARD_KEY_CAPS_LOCK:
        case KEYBOARD_KEY_NUM_LOCK:
        case KEYBOARD_KEY_SCROLL_LOCK:
        case KEYBOARD_KEY_F1:
        case KEYBOARD_KEY_F2:
        case KEYBOARD_KEY_F3:
            return 0;
        default:
            return 1;
    }
}

int usb_hid_usage_can_repeat(uint8_t usage) {
    return usb_hid_keycode_can_repeat(usb_hid_usage_to_keycode(usage));
}

uint8_t usb_hid_first_repeat_usage(const uint8_t report[8]) {
    if (report == 0) {
        return 0;
    }
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t usage = report[i];

        if (usage != 0u && usb_hid_usage_can_repeat(usage)) {
            return usage;
        }
    }
    return 0;
}
