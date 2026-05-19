#include "drivers/input/keyboard.h"

static uint8_t g_shift_active;
static uint8_t g_caps_lock_active;
static uint8_t g_num_lock_active;
static uint8_t g_scroll_lock_active;
static uint8_t g_ctrl_active;
static uint8_t g_alt_active;
static uint8_t g_extended_prefix_active;

#define KEYBOARD_EVENT_QUEUE_SIZE 64u

static struct keyboard_event_record g_event_queue[KEYBOARD_EVENT_QUEUE_SIZE];
static volatile uint32_t g_event_head;
static volatile uint32_t g_event_tail;
static volatile uint32_t g_event_count;
static volatile uint32_t g_event_dropped;
static volatile uint32_t g_event_seq;

static int keyboard_keycode_is_letter(enum keyboard_keycode keycode) {
    return keycode >= KEYBOARD_KEY_A && keycode <= KEYBOARD_KEY_Z;
}

static enum keyboard_keycode keyboard_lookup_keycode(uint8_t scancode, int extended) {
    static const enum keyboard_keycode base_map[128] = {
        [0x01] = KEYBOARD_KEY_ESC,
        [0x02] = KEYBOARD_KEY_1, [0x03] = KEYBOARD_KEY_2, [0x04] = KEYBOARD_KEY_3, [0x05] = KEYBOARD_KEY_4,
        [0x06] = KEYBOARD_KEY_5, [0x07] = KEYBOARD_KEY_6, [0x08] = KEYBOARD_KEY_7, [0x09] = KEYBOARD_KEY_8,
        [0x0a] = KEYBOARD_KEY_9, [0x0b] = KEYBOARD_KEY_0, [0x0c] = KEYBOARD_KEY_MINUS, [0x0d] = KEYBOARD_KEY_EQUAL,
        [0x0e] = KEYBOARD_KEY_BACKSPACE,
        [0x0f] = KEYBOARD_KEY_TAB,
        [0x10] = KEYBOARD_KEY_Q, [0x11] = KEYBOARD_KEY_W, [0x12] = KEYBOARD_KEY_E, [0x13] = KEYBOARD_KEY_R,
        [0x14] = KEYBOARD_KEY_T, [0x15] = KEYBOARD_KEY_Y, [0x16] = KEYBOARD_KEY_U, [0x17] = KEYBOARD_KEY_I,
        [0x18] = KEYBOARD_KEY_O, [0x19] = KEYBOARD_KEY_P, [0x1a] = KEYBOARD_KEY_LEFT_BRACKET,
        [0x1b] = KEYBOARD_KEY_RIGHT_BRACKET,
        [0x1c] = KEYBOARD_KEY_ENTER,
        [0x1d] = KEYBOARD_KEY_LEFT_CTRL,
        [0x1e] = KEYBOARD_KEY_A, [0x1f] = KEYBOARD_KEY_S, [0x20] = KEYBOARD_KEY_D, [0x21] = KEYBOARD_KEY_F,
        [0x22] = KEYBOARD_KEY_G, [0x23] = KEYBOARD_KEY_H, [0x24] = KEYBOARD_KEY_J, [0x25] = KEYBOARD_KEY_K,
        [0x26] = KEYBOARD_KEY_L, [0x27] = KEYBOARD_KEY_SEMICOLON, [0x28] = KEYBOARD_KEY_APOSTROPHE,
        [0x29] = KEYBOARD_KEY_GRAVE,
        [0x2a] = KEYBOARD_KEY_LEFT_SHIFT,
        [0x2b] = KEYBOARD_KEY_BACKSLASH,
        [0x2c] = KEYBOARD_KEY_Z, [0x2d] = KEYBOARD_KEY_X, [0x2e] = KEYBOARD_KEY_C, [0x2f] = KEYBOARD_KEY_V,
        [0x30] = KEYBOARD_KEY_B, [0x31] = KEYBOARD_KEY_N, [0x32] = KEYBOARD_KEY_M, [0x33] = KEYBOARD_KEY_COMMA,
        [0x34] = KEYBOARD_KEY_PERIOD, [0x35] = KEYBOARD_KEY_SLASH,
        [0x36] = KEYBOARD_KEY_RIGHT_SHIFT,
        [0x39] = KEYBOARD_KEY_SPACE,
        [0x3a] = KEYBOARD_KEY_CAPS_LOCK,
        [0x38] = KEYBOARD_KEY_LEFT_ALT,
        [0x3b] = KEYBOARD_KEY_F1,
        [0x3c] = KEYBOARD_KEY_F2,
        [0x3d] = KEYBOARD_KEY_F3,
        [0x45] = KEYBOARD_KEY_NUM_LOCK,
        [0x46] = KEYBOARD_KEY_SCROLL_LOCK,
        [0x47] = KEYBOARD_KEY_HOME,
        [0x48] = KEYBOARD_KEY_UP,
        [0x49] = KEYBOARD_KEY_PAGE_UP,
        [0x4b] = KEYBOARD_KEY_LEFT,
        [0x4d] = KEYBOARD_KEY_RIGHT,
        [0x4f] = KEYBOARD_KEY_END,
        [0x50] = KEYBOARD_KEY_DOWN,
        [0x51] = KEYBOARD_KEY_PAGE_DOWN,
        [0x53] = KEYBOARD_KEY_DELETE
    };
    static const enum keyboard_keycode extended_map[128] = {
        [0x1c] = KEYBOARD_KEY_ENTER,
        [0x1d] = KEYBOARD_KEY_RIGHT_CTRL,
        [0x38] = KEYBOARD_KEY_RIGHT_ALT,
        [0x47] = KEYBOARD_KEY_HOME,
        [0x48] = KEYBOARD_KEY_UP,
        [0x49] = KEYBOARD_KEY_PAGE_UP,
        [0x4b] = KEYBOARD_KEY_LEFT,
        [0x4d] = KEYBOARD_KEY_RIGHT,
        [0x4f] = KEYBOARD_KEY_END,
        [0x50] = KEYBOARD_KEY_DOWN,
        [0x51] = KEYBOARD_KEY_PAGE_DOWN,
        [0x53] = KEYBOARD_KEY_DELETE
    };

    if (scancode >= 128u) {
        return KEYBOARD_KEY_NONE;
    }
    return extended ? extended_map[scancode] : base_map[scancode];
}

static char keyboard_keycode_to_ascii(enum keyboard_keycode keycode, int shift_active, int caps_lock_active) {
    static const char unshifted_map[] = {
        [KEYBOARD_KEY_TAB] = '\t',
        [KEYBOARD_KEY_SPACE] = ' ',
        [KEYBOARD_KEY_0] = '0', [KEYBOARD_KEY_1] = '1', [KEYBOARD_KEY_2] = '2', [KEYBOARD_KEY_3] = '3',
        [KEYBOARD_KEY_4] = '4', [KEYBOARD_KEY_5] = '5', [KEYBOARD_KEY_6] = '6', [KEYBOARD_KEY_7] = '7',
        [KEYBOARD_KEY_8] = '8', [KEYBOARD_KEY_9] = '9',
        [KEYBOARD_KEY_A] = 'a', [KEYBOARD_KEY_B] = 'b', [KEYBOARD_KEY_C] = 'c', [KEYBOARD_KEY_D] = 'd',
        [KEYBOARD_KEY_E] = 'e', [KEYBOARD_KEY_F] = 'f', [KEYBOARD_KEY_G] = 'g', [KEYBOARD_KEY_H] = 'h',
        [KEYBOARD_KEY_I] = 'i', [KEYBOARD_KEY_J] = 'j', [KEYBOARD_KEY_K] = 'k', [KEYBOARD_KEY_L] = 'l',
        [KEYBOARD_KEY_M] = 'm', [KEYBOARD_KEY_N] = 'n', [KEYBOARD_KEY_O] = 'o', [KEYBOARD_KEY_P] = 'p',
        [KEYBOARD_KEY_Q] = 'q', [KEYBOARD_KEY_R] = 'r', [KEYBOARD_KEY_S] = 's', [KEYBOARD_KEY_T] = 't',
        [KEYBOARD_KEY_U] = 'u', [KEYBOARD_KEY_V] = 'v', [KEYBOARD_KEY_W] = 'w', [KEYBOARD_KEY_X] = 'x',
        [KEYBOARD_KEY_Y] = 'y', [KEYBOARD_KEY_Z] = 'z',
        [KEYBOARD_KEY_MINUS] = '-', [KEYBOARD_KEY_EQUAL] = '=',
        [KEYBOARD_KEY_LEFT_BRACKET] = '[', [KEYBOARD_KEY_RIGHT_BRACKET] = ']',
        [KEYBOARD_KEY_BACKSLASH] = '\\',
        [KEYBOARD_KEY_SEMICOLON] = ';', [KEYBOARD_KEY_APOSTROPHE] = '\'',
        [KEYBOARD_KEY_GRAVE] = '`',
        [KEYBOARD_KEY_COMMA] = ',', [KEYBOARD_KEY_PERIOD] = '.', [KEYBOARD_KEY_SLASH] = '/'
    };
    static const char shifted_map[] = {
        [KEYBOARD_KEY_TAB] = '\t',
        [KEYBOARD_KEY_SPACE] = ' ',
        [KEYBOARD_KEY_0] = ')', [KEYBOARD_KEY_1] = '!', [KEYBOARD_KEY_2] = '@', [KEYBOARD_KEY_3] = '#',
        [KEYBOARD_KEY_4] = '$', [KEYBOARD_KEY_5] = '%', [KEYBOARD_KEY_6] = '^', [KEYBOARD_KEY_7] = '&',
        [KEYBOARD_KEY_8] = '*', [KEYBOARD_KEY_9] = '(',
        [KEYBOARD_KEY_A] = 'A', [KEYBOARD_KEY_B] = 'B', [KEYBOARD_KEY_C] = 'C', [KEYBOARD_KEY_D] = 'D',
        [KEYBOARD_KEY_E] = 'E', [KEYBOARD_KEY_F] = 'F', [KEYBOARD_KEY_G] = 'G', [KEYBOARD_KEY_H] = 'H',
        [KEYBOARD_KEY_I] = 'I', [KEYBOARD_KEY_J] = 'J', [KEYBOARD_KEY_K] = 'K', [KEYBOARD_KEY_L] = 'L',
        [KEYBOARD_KEY_M] = 'M', [KEYBOARD_KEY_N] = 'N', [KEYBOARD_KEY_O] = 'O', [KEYBOARD_KEY_P] = 'P',
        [KEYBOARD_KEY_Q] = 'Q', [KEYBOARD_KEY_R] = 'R', [KEYBOARD_KEY_S] = 'S', [KEYBOARD_KEY_T] = 'T',
        [KEYBOARD_KEY_U] = 'U', [KEYBOARD_KEY_V] = 'V', [KEYBOARD_KEY_W] = 'W', [KEYBOARD_KEY_X] = 'X',
        [KEYBOARD_KEY_Y] = 'Y', [KEYBOARD_KEY_Z] = 'Z',
        [KEYBOARD_KEY_MINUS] = '_', [KEYBOARD_KEY_EQUAL] = '+',
        [KEYBOARD_KEY_LEFT_BRACKET] = '{', [KEYBOARD_KEY_RIGHT_BRACKET] = '}',
        [KEYBOARD_KEY_BACKSLASH] = '|',
        [KEYBOARD_KEY_SEMICOLON] = ':', [KEYBOARD_KEY_APOSTROPHE] = '"',
        [KEYBOARD_KEY_GRAVE] = '~',
        [KEYBOARD_KEY_COMMA] = '<', [KEYBOARD_KEY_PERIOD] = '>', [KEYBOARD_KEY_SLASH] = '?'
    };
    char ch;

    if (keycode <= KEYBOARD_KEY_NONE || keycode >= (enum keyboard_keycode)(sizeof(unshifted_map) / sizeof(unshifted_map[0]))) {
        return 0;
    }

    ch = shift_active ? shifted_map[keycode] : unshifted_map[keycode];
    if (ch == 0) {
        return 0;
    }
    if (keyboard_keycode_is_letter(keycode) && caps_lock_active) {
        if (shift_active) {
            return (char)(ch - 'A' + 'a');
        }
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

struct keyboard_event keyboard_handle_scancode(uint8_t scancode) {
    struct keyboard_event event;
    uint8_t code = (uint8_t)(scancode & 0x7fu);
    int release = (scancode & 0x80u) != 0u;
    int extended = 0;

    event.keycode = KEYBOARD_KEY_NONE;
    event.ascii = 0;
    event.pressed = 0;
    event.released = 0;
    event.extended = 0;
    event.shift = g_shift_active;
    event.ctrl = g_ctrl_active;
    event.alt = g_alt_active;
    event.caps_lock = g_caps_lock_active;
    event.num_lock = g_num_lock_active;
    event.scroll_lock = g_scroll_lock_active;

    if (scancode == 0xe0u) {
        g_extended_prefix_active = 1u;
        return event;
    }

    extended = g_extended_prefix_active != 0u;
    g_extended_prefix_active = 0u;
    event.extended = extended ? 1u : 0u;
    event.keycode = keyboard_lookup_keycode(code, extended);
    if (event.keycode == KEYBOARD_KEY_NONE) {
        return event;
    }

    switch (event.keycode) {
        case KEYBOARD_KEY_LEFT_SHIFT:
        case KEYBOARD_KEY_RIGHT_SHIFT:
            g_shift_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_LEFT_CTRL:
        case KEYBOARD_KEY_RIGHT_CTRL:
            g_ctrl_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_LEFT_ALT:
        case KEYBOARD_KEY_RIGHT_ALT:
            g_alt_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_CAPS_LOCK:
            if (!release) {
                g_caps_lock_active ^= 1u;
            }
            break;
        case KEYBOARD_KEY_NUM_LOCK:
            if (!release) {
                g_num_lock_active ^= 1u;
            }
            break;
        case KEYBOARD_KEY_SCROLL_LOCK:
            if (!release) {
                g_scroll_lock_active ^= 1u;
            }
            break;
        default:
            break;
    }

    event.pressed = release ? 0u : 1u;
    event.released = release ? 1u : 0u;
    event.shift = g_shift_active;
    event.ctrl = g_ctrl_active;
    event.alt = g_alt_active;
    event.caps_lock = g_caps_lock_active;
    event.num_lock = g_num_lock_active;
    event.scroll_lock = g_scroll_lock_active;

    if (!release) {
        event.ascii = keyboard_keycode_to_ascii(event.keycode, event.shift != 0, event.caps_lock != 0);
    }
    return event;
}

struct keyboard_event keyboard_handle_keycode(enum keyboard_keycode keycode, int pressed) {
    struct keyboard_event event;
    int release = !pressed;

    event.keycode = keycode;
    event.ascii = 0;
    event.pressed = pressed ? 1u : 0u;
    event.released = release ? 1u : 0u;
    event.extended = 0;
    event.shift = g_shift_active;
    event.ctrl = g_ctrl_active;
    event.alt = g_alt_active;
    event.caps_lock = g_caps_lock_active;
    event.num_lock = g_num_lock_active;
    event.scroll_lock = g_scroll_lock_active;

    if (keycode == KEYBOARD_KEY_NONE) {
        return event;
    }

    switch (keycode) {
        case KEYBOARD_KEY_LEFT_SHIFT:
        case KEYBOARD_KEY_RIGHT_SHIFT:
            g_shift_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_LEFT_CTRL:
        case KEYBOARD_KEY_RIGHT_CTRL:
            g_ctrl_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_LEFT_ALT:
        case KEYBOARD_KEY_RIGHT_ALT:
            g_alt_active = release ? 0u : 1u;
            break;
        case KEYBOARD_KEY_CAPS_LOCK:
            if (!release) {
                g_caps_lock_active ^= 1u;
            }
            break;
        case KEYBOARD_KEY_NUM_LOCK:
            if (!release) {
                g_num_lock_active ^= 1u;
            }
            break;
        case KEYBOARD_KEY_SCROLL_LOCK:
            if (!release) {
                g_scroll_lock_active ^= 1u;
            }
            break;
        default:
            break;
    }

    event.shift = g_shift_active;
    event.ctrl = g_ctrl_active;
    event.alt = g_alt_active;
    event.caps_lock = g_caps_lock_active;
    event.num_lock = g_num_lock_active;
    event.scroll_lock = g_scroll_lock_active;
    if (!release) {
        event.ascii = keyboard_keycode_to_ascii(event.keycode, event.shift != 0, event.caps_lock != 0);
    }
    return event;
}

int keyboard_is_ctrl_active(void) {
    return g_ctrl_active != 0;
}

uint8_t keyboard_led_state(void) {
    return (uint8_t)((g_num_lock_active ? 1u : 0u) |
                     (g_caps_lock_active ? 2u : 0u) |
                     (g_scroll_lock_active ? 4u : 0u));
}

void keyboard_event_queue_push(const struct keyboard_event *event, uint32_t tick) {
    struct keyboard_event_record *slot;
    uint32_t head;

    if (event == 0 || event->keycode == KEYBOARD_KEY_NONE) {
        return;
    }
    if (g_event_count >= KEYBOARD_EVENT_QUEUE_SIZE) {
        g_event_tail = (g_event_tail + 1u) % KEYBOARD_EVENT_QUEUE_SIZE;
        g_event_count--;
        g_event_dropped++;
    }
    head = g_event_head;
    slot = &g_event_queue[head];
    slot->seq = ++g_event_seq;
    slot->tick = tick;
    slot->event = *event;
    g_event_head = (head + 1u) % KEYBOARD_EVENT_QUEUE_SIZE;
    g_event_count++;
}

int keyboard_event_queue_pop(struct keyboard_event_record *out) {
    uint32_t tail;

    if (out == 0 || g_event_count == 0) {
        return 0;
    }
    tail = g_event_tail;
    *out = g_event_queue[tail];
    g_event_tail = (tail + 1u) % KEYBOARD_EVENT_QUEUE_SIZE;
    g_event_count--;
    return 1;
}

int keyboard_event_queue_get_after(uint32_t *cursor_io, struct keyboard_event_record *out) {
    uint32_t index;

    if (cursor_io == 0 || out == 0 || g_event_count == 0) {
        return 0;
    }
    index = g_event_tail;
    for (uint32_t i = 0; i < g_event_count; i++) {
        const struct keyboard_event_record *rec = &g_event_queue[index];

        if (rec->seq > *cursor_io) {
            *out = *rec;
            *cursor_io = rec->seq;
            return 1;
        }
        index = (index + 1u) % KEYBOARD_EVENT_QUEUE_SIZE;
    }
    return 0;
}

uint32_t keyboard_event_queue_pending(void) {
    return g_event_count;
}

uint32_t keyboard_event_queue_dropped(void) {
    return g_event_dropped;
}

uint32_t keyboard_event_queue_latest_seq(void) {
    return g_event_seq;
}
