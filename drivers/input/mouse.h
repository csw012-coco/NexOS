#pragma once

#include <stdint.h>

struct mouse_event_record {
    uint32_t seq;
    uint32_t tick;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
};

struct mouse_status {
    uint32_t initialized;
    uint32_t ps2_enabled;
    uint32_t packet_index;
    uint32_t pending;
    uint32_t dropped;
    uint32_t latest_seq;
    uint32_t packets;
    uint32_t events;
    uint32_t bad_packets;
    uint32_t resync_count;
    uint32_t init_failures;
    uint32_t command_count;
    uint32_t ack_count;
    uint32_t resend_count;
    uint32_t timeout_count;
    uint32_t last_status;
    uint32_t last_error;
};

void mouse_init(void);
void mouse_set_event_callback(void (*callback)(const struct mouse_event_record *event));
void mouse_push_event(int32_t dx, int32_t dy, uint8_t buttons, uint32_t tick);
void mouse_handle_data(uint8_t data, uint32_t tick);
void mouse_poll_ps2(uint32_t tick);
int mouse_query_status(struct mouse_status *out);
int mouse_event_get_after(uint32_t *cursor_io, struct mouse_event_record *out);
uint32_t mouse_event_pending(void);
uint32_t mouse_event_dropped(void);
uint32_t mouse_event_latest_seq(void);
