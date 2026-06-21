#pragma once

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;

struct i386_key_event {
    uint8_t scancode;
    uint8_t pressed;
    char ascii;
};

void i386_keyboard_init(void);
void i386_keyboard_handle_irq(void);
int i386_keyboard_pop(struct i386_key_event *event);
uint32_t i386_keyboard_irq_count(void);
uint32_t i386_keyboard_dropped(void);
int i386_keyboard_inject_scancode(uint8_t scancode);
