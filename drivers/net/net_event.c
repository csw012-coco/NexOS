#include "drivers/net/net_event.h"

#include "kernel/public/proc/scheduler.h"

#define NET_STATUS_EVENT_QUEUE_SIZE 16u

static struct net_status_event_record g_status_event_queue[NET_STATUS_EVENT_QUEUE_SIZE];
static uint32_t g_status_event_head;
static uint32_t g_status_event_tail;
static uint32_t g_status_event_count;
static uint32_t g_status_event_dropped;
static uint32_t g_status_event_seq;
static uint8_t g_status_event_known;
static uint8_t g_status_event_last_present;
static uint8_t g_status_event_last_initialized;
static uint8_t g_status_event_last_link_up;
static uint16_t g_status_event_last_speed_mbps;

void net_event_emit_status(uint8_t present, uint8_t initialized, uint8_t link_up, uint16_t speed_mbps) {
    struct net_status_event_record *slot;
    uint32_t head;

    if (g_status_event_known &&
        g_status_event_last_present == present &&
        g_status_event_last_initialized == initialized &&
        g_status_event_last_link_up == link_up &&
        g_status_event_last_speed_mbps == speed_mbps) {
        return;
    }
    g_status_event_known = 1u;
    g_status_event_last_present = present;
    g_status_event_last_initialized = initialized;
    g_status_event_last_link_up = link_up;
    g_status_event_last_speed_mbps = speed_mbps;

    if (g_status_event_count >= NET_STATUS_EVENT_QUEUE_SIZE) {
        g_status_event_tail = (g_status_event_tail + 1u) % NET_STATUS_EVENT_QUEUE_SIZE;
        g_status_event_count--;
        g_status_event_dropped++;
    }
    head = g_status_event_head;
    slot = &g_status_event_queue[head];
    slot->seq = ++g_status_event_seq;
    slot->tick = sched_current_ticks();
    slot->present = present;
    slot->initialized = initialized;
    slot->link_up = link_up;
    slot->speed_mbps = speed_mbps;
    g_status_event_head = (head + 1u) % NET_STATUS_EVENT_QUEUE_SIZE;
    g_status_event_count++;
}

int net_event_status_pop(struct net_status_event_record *out) {
    uint32_t tail;

    if (out == 0 || g_status_event_count == 0) {
        return 0;
    }
    tail = g_status_event_tail;
    *out = g_status_event_queue[tail];
    g_status_event_tail = (tail + 1u) % NET_STATUS_EVENT_QUEUE_SIZE;
    g_status_event_count--;
    return 1;
}

int net_event_status_get_after(uint32_t *cursor_io, struct net_status_event_record *out) {
    uint32_t index;

    if (cursor_io == 0 || out == 0 || g_status_event_count == 0) {
        return 0;
    }
    index = g_status_event_tail;
    for (uint32_t i = 0; i < g_status_event_count; i++) {
        const struct net_status_event_record *rec = &g_status_event_queue[index];

        if (rec->seq > *cursor_io) {
            *out = *rec;
            *cursor_io = rec->seq;
            return 1;
        }
        index = (index + 1u) % NET_STATUS_EVENT_QUEUE_SIZE;
    }
    return 0;
}

uint32_t net_event_status_pending(void) {
    return g_status_event_count;
}

uint32_t net_event_status_dropped(void) {
    return g_status_event_dropped;
}

uint32_t net_event_status_latest_seq(void) {
    return g_status_event_seq;
}
