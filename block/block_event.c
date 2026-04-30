#include "block/block_event.h"

#include "kernel/public/proc/scheduler.h"

#define BLOCK_CHANGE_EVENT_QUEUE_SIZE 32u

static struct block_change_event_record g_block_events[BLOCK_CHANGE_EVENT_QUEUE_SIZE];
static uint32_t g_block_head;
static uint32_t g_block_tail;
static uint32_t g_block_count;
static uint32_t g_block_dropped;
static uint32_t g_block_seq;

static void block_event_copy(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i < dst_size - 1u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void block_event_emit_change(const char *op, uint32_t disk, uint32_t part, const char *name, uint64_t blocks) {
    struct block_change_event_record *slot;
    uint32_t head;

    if (g_block_count >= BLOCK_CHANGE_EVENT_QUEUE_SIZE) {
        g_block_tail = (g_block_tail + 1u) % BLOCK_CHANGE_EVENT_QUEUE_SIZE;
        g_block_count--;
        g_block_dropped++;
    }
    head = g_block_head;
    slot = &g_block_events[head];
    slot->seq = ++g_block_seq;
    slot->tick = sched_current_ticks();
    slot->disk = disk;
    slot->part = part;
    slot->blocks = blocks;
    block_event_copy(slot->op, sizeof(slot->op), op);
    block_event_copy(slot->name, sizeof(slot->name), name);
    g_block_head = (head + 1u) % BLOCK_CHANGE_EVENT_QUEUE_SIZE;
    g_block_count++;
}

int block_event_change_get_after(uint32_t *cursor_io, struct block_change_event_record *out) {
    uint32_t index;

    if (cursor_io == 0 || out == 0 || g_block_count == 0) {
        return 0;
    }
    index = g_block_tail;
    for (uint32_t i = 0; i < g_block_count; i++) {
        const struct block_change_event_record *rec = &g_block_events[index];

        if (rec->seq > *cursor_io) {
            *out = *rec;
            *cursor_io = rec->seq;
            return 1;
        }
        index = (index + 1u) % BLOCK_CHANGE_EVENT_QUEUE_SIZE;
    }
    return 0;
}

uint32_t block_event_change_pending(void) {
    return g_block_count;
}

uint32_t block_event_change_dropped(void) {
    return g_block_dropped;
}

uint32_t block_event_change_latest_seq(void) {
    return g_block_seq;
}
