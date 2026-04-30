#pragma once

#include <stdint.h>

struct net_status_event_record {
    uint32_t seq;
    uint32_t tick;
    uint8_t present;
    uint8_t initialized;
    uint8_t link_up;
    uint8_t reserved0;
    uint16_t speed_mbps;
};

void net_event_emit_status(uint8_t present, uint8_t initialized, uint8_t link_up, uint16_t speed_mbps);
int net_event_status_pop(struct net_status_event_record *out);
int net_event_status_get_after(uint32_t *cursor_io, struct net_status_event_record *out);
uint32_t net_event_status_pending(void);
uint32_t net_event_status_dropped(void);
uint32_t net_event_status_latest_seq(void);
