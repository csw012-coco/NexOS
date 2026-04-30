#include "drivers/input/mouse.h"

#include "hal/hal.h"

#define MOUSE_EVENT_QUEUE_SIZE 64u

static struct mouse_event_record g_mouse_events[MOUSE_EVENT_QUEUE_SIZE];
static uint32_t g_mouse_head;
static uint32_t g_mouse_tail;
static uint32_t g_mouse_count;
static uint32_t g_mouse_dropped;
static uint32_t g_mouse_seq;
static uint8_t g_packet[3];
static uint8_t g_packet_index;

static void mouse_wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((hal_io_in8(0x64) & 0x02u) == 0u) {
            return;
        }
    }
}

static void mouse_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((hal_io_in8(0x64) & 0x01u) != 0u) {
            return;
        }
    }
}

static void mouse_write_device(uint8_t value) {
    mouse_wait_input_empty();
    hal_io_out8(0x64, 0xd4u);
    mouse_wait_input_empty();
    hal_io_out8(0x60, value);
    mouse_wait_output_full();
    (void)hal_io_in8(0x60);
}

void mouse_init(void) {
    mouse_wait_input_empty();
    hal_io_out8(0x64, 0xa8u);
    mouse_wait_input_empty();
    hal_io_out8(0x64, 0x20u);
    mouse_wait_output_full();
    {
        uint8_t status = (uint8_t)(hal_io_in8(0x60) | 0x02u);

        mouse_wait_input_empty();
        hal_io_out8(0x64, 0x60u);
        mouse_wait_input_empty();
        hal_io_out8(0x60, status);
    }
    mouse_write_device(0xf6u);
    mouse_write_device(0xf4u);
    hal_irq_set_mask(12u, 0);
}

static void mouse_event_push(int32_t dx, int32_t dy, uint8_t buttons, uint32_t tick) {
    struct mouse_event_record *slot;
    uint32_t head;

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
}

void mouse_handle_data(uint8_t data, uint32_t tick) {
    if (g_packet_index == 0u && (data & 0x08u) == 0u) {
        return;
    }
    g_packet[g_packet_index++] = data;
    if (g_packet_index < 3u) {
        return;
    }
    g_packet_index = 0;
    {
        int32_t dx = (int8_t)g_packet[1];
        int32_t dy = -(int32_t)(int8_t)g_packet[2];
        uint8_t buttons = (uint8_t)(g_packet[0] & 0x07u);

        mouse_event_push(dx, dy, buttons, tick);
    }
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
