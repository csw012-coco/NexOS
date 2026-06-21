#include "arch/x86/io.h"
#include "keyboard.h"

enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
    PS2_COMMAND = 0x64,
    PS2_STATUS_OUTPUT_FULL = 0x01,
    PS2_STATUS_INPUT_FULL = 0x02,
    PS2_COMMAND_WRITE_OUTPUT = 0xd2,
    KEY_QUEUE_SIZE = 32,
    PS2_WAIT_LIMIT = 100000
};

static struct i386_key_event key_queue[KEY_QUEUE_SIZE];
static volatile uint32_t key_head;
static volatile uint32_t key_tail;
static volatile uint32_t key_irq_count;
static volatile uint32_t key_dropped;

static const char set1_ascii[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = '\n',
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2b] = '\\',
    [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static int ps2_wait_input_empty(void) {
    for (uint32_t i = 0; i < PS2_WAIT_LIMIT; i++) {
        if ((inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL) == 0u) {
            return 1;
        }
    }
    return 0;
}

static void keyboard_queue_push(uint8_t scancode) {
    uint32_t next = (key_head + 1u) % KEY_QUEUE_SIZE;
    uint8_t code = (uint8_t)(scancode & 0x7fu);
    struct i386_key_event event;

    if (next == key_tail) {
        key_dropped++;
        return;
    }

    event.scancode = scancode;
    event.pressed = (uint8_t)((scancode & 0x80u) == 0u);
    event.ascii = event.pressed ? set1_ascii[code] : 0;
    key_queue[key_head] = event;
    key_head = next;
}

void i386_keyboard_init(void) {
    key_head = 0;
    key_tail = 0;
    key_irq_count = 0;
    key_dropped = 0;

    while ((inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0u) {
        (void)inb(PS2_DATA);
    }
}

void i386_keyboard_handle_irq(void) {
    if ((inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0u) {
        uint8_t scancode = inb(PS2_DATA);
        key_irq_count++;
        keyboard_queue_push(scancode);
    }
}

int i386_keyboard_pop(struct i386_key_event *event) {
    if (event == 0 || key_tail == key_head) {
        return 0;
    }
    *event = key_queue[key_tail];
    key_tail = (key_tail + 1u) % KEY_QUEUE_SIZE;
    return 1;
}

uint32_t i386_keyboard_irq_count(void) {
    return key_irq_count;
}

uint32_t i386_keyboard_dropped(void) {
    return key_dropped;
}

int i386_keyboard_inject_scancode(uint8_t scancode) {
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    outb(PS2_COMMAND, PS2_COMMAND_WRITE_OUTPUT);
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    outb(PS2_DATA, scancode);
    return 1;
}
