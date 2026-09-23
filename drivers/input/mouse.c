#include "drivers/input/mouse.h"

#include "hal/hal.h"

#define MOUSE_EVENT_QUEUE_SIZE 64u

static struct mouse_event_record g_mouse_events[MOUSE_EVENT_QUEUE_SIZE];
static uint32_t g_mouse_head;
static uint32_t g_mouse_tail;
static uint32_t g_mouse_count;
static uint32_t g_mouse_dropped;
static uint32_t g_mouse_seq;
static uint8_t g_mouse_last_buttons;
static uint8_t g_packet[3];
static uint8_t g_packet_index;
static void (*g_mouse_event_callback)(const struct mouse_event_record *event);
static uint8_t g_mouse_initialized;
static uint8_t g_mouse_ps2_enabled;
static uint32_t g_mouse_packets;
static uint32_t g_mouse_event_count;
static uint32_t g_mouse_bad_packets;
static uint32_t g_mouse_resync_count;
static uint32_t g_mouse_init_failures;
static uint32_t g_mouse_command_count;
static uint32_t g_mouse_ack_count;
static uint32_t g_mouse_resend_count;
static uint32_t g_mouse_timeout_count;
static uint8_t g_mouse_last_status;
static uint8_t g_mouse_last_error;

enum {
    MOUSE_ERROR_NONE = 0u,
    MOUSE_ERROR_INPUT_TIMEOUT = 1u,
    MOUSE_ERROR_OUTPUT_TIMEOUT = 2u,
    MOUSE_ERROR_BAD_ACK = 3u,
    MOUSE_ERROR_PACKET_SYNC = 4u,
    MOUSE_ERROR_PACKET_OVERFLOW = 5u
};

static int mouse_wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        uint8_t status = hal_io_in8(0x64);

        g_mouse_last_status = status;
        if ((status & 0x02u) == 0u) {
            return 1;
        }
    }
    g_mouse_timeout_count++;
    g_mouse_last_error = MOUSE_ERROR_INPUT_TIMEOUT;
    return 0;
}

static int mouse_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        uint8_t status = hal_io_in8(0x64);

        g_mouse_last_status = status;
        if ((status & 0x01u) != 0u) {
            return 1;
        }
    }
    g_mouse_timeout_count++;
    g_mouse_last_error = MOUSE_ERROR_OUTPUT_TIMEOUT;
    return 0;
}

static int mouse_write_device(uint8_t value) {
    for (uint32_t attempt = 0; attempt < 2u; attempt++) {
        uint8_t ack;

        g_mouse_command_count++;
        if (!mouse_wait_input_empty()) {
            return 0;
        }
        hal_io_out8(0x64, 0xd4u);
        if (!mouse_wait_input_empty()) {
            return 0;
        }
        hal_io_out8(0x60, value);
        if (!mouse_wait_output_full()) {
            return 0;
        }
        ack = hal_io_in8(0x60);
        if (ack == 0xfau) {
            g_mouse_ack_count++;
            return 1;
        }
        if (ack != 0xfeu) {
            g_mouse_last_error = MOUSE_ERROR_BAD_ACK;
            return 0;
        }
        g_mouse_resend_count++;
    }
    g_mouse_last_error = MOUSE_ERROR_BAD_ACK;
    return 0;
}

void mouse_init(void) {
    g_mouse_initialized = 0u;
    g_mouse_ps2_enabled = 0u;
    g_packet_index = 0u;
    g_mouse_last_error = MOUSE_ERROR_NONE;
    if (!mouse_wait_input_empty()) {
        g_mouse_init_failures++;
        return;
    }
    hal_io_out8(0x64, 0xa8u);
    if (!mouse_wait_input_empty()) {
        g_mouse_init_failures++;
        return;
    }
    hal_io_out8(0x64, 0x20u);
    if (!mouse_wait_output_full()) {
        g_mouse_init_failures++;
        return;
    }
    {
        uint8_t status = (uint8_t)((hal_io_in8(0x60) | 0x02u) & ~0x20u);

        if (!mouse_wait_input_empty()) {
            g_mouse_init_failures++;
            return;
        }
        hal_io_out8(0x64, 0x60u);
        if (!mouse_wait_input_empty()) {
            g_mouse_init_failures++;
            return;
        }
        hal_io_out8(0x60, status);
    }
    if (!mouse_write_device(0xf6u) || !mouse_write_device(0xf4u)) {
        g_mouse_init_failures++;
        return;
    }
    g_mouse_initialized = 1u;
    g_mouse_ps2_enabled = 1u;
    hal_irq_set_mask(12u, 0);
}

void mouse_set_event_callback(void (*callback)(const struct mouse_event_record *event)) {
    g_mouse_event_callback = callback;
}

void mouse_push_event(int32_t dx, int32_t dy, uint8_t buttons, uint32_t tick) {
    struct mouse_event_record *slot;
    uint32_t head;

    if (dx == 0 && dy == 0) {
        if (buttons == g_mouse_last_buttons) {
            return;
        }
    }
    g_mouse_last_buttons = buttons;
    if (g_mouse_count >= MOUSE_EVENT_QUEUE_SIZE) {
        g_mouse_tail = (g_mouse_tail + 1u) % MOUSE_EVENT_QUEUE_SIZE;
        g_mouse_count--;
        g_mouse_dropped++;
    }
    head = g_mouse_head;
    slot = &g_mouse_events[head];
    slot->seq = ++g_mouse_seq;
    slot->tick = tick;
    slot->dx = dx;
    slot->dy = dy;
    slot->buttons = buttons;
    g_mouse_head = (head + 1u) % MOUSE_EVENT_QUEUE_SIZE;
    g_mouse_count++;
    g_mouse_event_count++;
    if (g_mouse_event_callback != 0) {
        g_mouse_event_callback(slot);
    }
}

void mouse_handle_data(uint8_t data, uint32_t tick) {
    if (g_packet_index == 0u && (data & 0x08u) == 0u) {
        g_mouse_bad_packets++;
        g_mouse_resync_count++;
        g_mouse_last_error = MOUSE_ERROR_PACKET_SYNC;
        return;
    }
    g_packet[g_packet_index++] = data;
    if (g_packet_index < 3u) {
        return;
    }
    g_packet_index = 0;
    g_mouse_packets++;
    if ((g_packet[0] & 0xc0u) != 0u) {
        g_mouse_bad_packets++;
        g_mouse_last_error = MOUSE_ERROR_PACKET_OVERFLOW;
        return;
    }
    {
        int32_t dx = (int8_t)g_packet[1];
        int32_t dy = -(int32_t)(int8_t)g_packet[2];
        uint8_t buttons = (uint8_t)(g_packet[0] & 0x07u);

        mouse_push_event(dx, dy, buttons, tick);
    }
}

void mouse_poll_ps2(uint32_t tick) {
    for (uint32_t i = 0; i < 8u; i++) {
        uint8_t status = hal_io_in8(0x64);

        g_mouse_last_status = status;
        if ((status & 0x01u) == 0u) {
            return;
        }
        if ((status & 0x20u) == 0u) {
            return;
        }
        mouse_handle_data(hal_io_in8(0x60), tick);
    }
}

int mouse_query_status(struct mouse_status *out) {
    if (out == 0) {
        return 0;
    }
    out->initialized = g_mouse_initialized;
    out->ps2_enabled = g_mouse_ps2_enabled;
    out->packet_index = g_packet_index;
    out->pending = g_mouse_count;
    out->dropped = g_mouse_dropped;
    out->latest_seq = g_mouse_seq;
    out->packets = g_mouse_packets;
    out->events = g_mouse_event_count;
    out->bad_packets = g_mouse_bad_packets;
    out->resync_count = g_mouse_resync_count;
    out->init_failures = g_mouse_init_failures;
    out->command_count = g_mouse_command_count;
    out->ack_count = g_mouse_ack_count;
    out->resend_count = g_mouse_resend_count;
    out->timeout_count = g_mouse_timeout_count;
    out->last_status = g_mouse_last_status;
    out->last_error = g_mouse_last_error;
    return g_mouse_initialized != 0u;
}

int mouse_event_get_after(uint32_t *cursor_io, struct mouse_event_record *out) {
    uint32_t index;

    if (cursor_io == 0 || out == 0 || g_mouse_count == 0) {
        return 0;
    }
    index = g_mouse_tail;
    for (uint32_t i = 0; i < g_mouse_count; i++) {
        const struct mouse_event_record *rec = &g_mouse_events[index];

        if (rec->seq > *cursor_io) {
            *out = *rec;
            *cursor_io = rec->seq;
            return 1;
        }
        index = (index + 1u) % MOUSE_EVENT_QUEUE_SIZE;
    }
    return 0;
}

uint32_t mouse_event_pending(void) {
    return g_mouse_count;
}

uint32_t mouse_event_dropped(void) {
    return g_mouse_dropped;
}

uint32_t mouse_event_latest_seq(void) {
    return g_mouse_seq;
}
