#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

struct keyboard_event_record {
    uint32_t seq;
    uint32_t tick;
    struct keyboard_event event;
};

struct keyboard_status {
    uint32_t shift_active;
    uint32_t caps_lock_active;
    uint32_t num_lock_active;
    uint32_t scroll_lock_active;
    uint32_t ctrl_active;
    uint32_t alt_active;
    uint32_t extended_prefix_active;
    uint32_t pause_bytes_remaining;
    uint32_t pending;
    uint32_t dropped;
    uint32_t latest_seq;
    uint32_t scancode_count;
    uint32_t event_count;
    uint32_t ignored_scancode_count;
    uint32_t unknown_scancode_count;
    uint32_t last_scancode;
};

void keyboard_ps2_init(void);
struct keyboard_event keyboard_handle_scancode(uint8_t scancode);
struct keyboard_event keyboard_handle_keycode(enum keyboard_keycode keycode, int pressed);
int keyboard_is_ctrl_active(void);
uint8_t keyboard_led_state(void);
int keyboard_query_status(struct keyboard_status *out);
void keyboard_event_queue_push(const struct keyboard_event *event, uint32_t tick);
int keyboard_event_queue_pop(struct keyboard_event_record *out);
int keyboard_event_queue_get_after(uint32_t *cursor_io, struct keyboard_event_record *out);
uint32_t keyboard_event_queue_pending(void);
uint32_t keyboard_event_queue_dropped(void);
uint32_t keyboard_event_queue_latest_seq(void);
