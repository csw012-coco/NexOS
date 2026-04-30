#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

struct keyboard_event_record {
    uint32_t seq;
    uint32_t tick;
    struct keyboard_event event;
};

struct keyboard_event keyboard_handle_scancode(uint8_t scancode);
struct keyboard_event keyboard_handle_keycode(enum keyboard_keycode keycode, int pressed);
int keyboard_is_ctrl_active(void);
void keyboard_event_queue_push(const struct keyboard_event *event, uint32_t tick);
int keyboard_event_queue_pop(struct keyboard_event_record *out);
int keyboard_event_queue_get_after(uint32_t *cursor_io, struct keyboard_event_record *out);
uint32_t keyboard_event_queue_pending(void);
uint32_t keyboard_event_queue_dropped(void);
uint32_t keyboard_event_queue_latest_seq(void);
