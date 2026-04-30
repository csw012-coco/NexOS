#pragma once

#include <stdint.h>

struct block_change_event_record {
    uint32_t seq;
    uint32_t tick;
    uint32_t disk;
    uint32_t part;
    uint64_t blocks;
    char op[12];
    char name[16];
};

void block_event_emit_change(const char *op, uint32_t disk, uint32_t part, const char *name, uint64_t blocks);
int block_event_change_get_after(uint32_t *cursor_io, struct block_change_event_record *out);
uint32_t block_event_change_pending(void);
uint32_t block_event_change_dropped(void);
uint32_t block_event_change_latest_seq(void);
