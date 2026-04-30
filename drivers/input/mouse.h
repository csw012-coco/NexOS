#pragma once

#include <stdint.h>

struct mouse_event_record {
    uint32_t seq;
    uint32_t tick;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
};

void mouse_init(void);
void mouse_handle_data(uint8_t data, uint32_t tick);
int mouse_event_get_after(uint32_t *cursor_io, struct mouse_event_record *out);
uint32_t mouse_event_pending(void);
uint32_t mouse_event_dropped(void);
uint32_t mouse_event_latest_seq(void);
