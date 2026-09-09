#pragma once

#include <stdint.h>

struct lapic_status {
    uint8_t present;
    uint8_t mapped;
    uint8_t enabled;
    uint8_t msr_enabled;
    uint8_t id;
    uint64_t address;
};

int lapic_init_from_acpi(void);
int lapic_enable(void);
int lapic_query_status(struct lapic_status *out);
uint8_t lapic_current_id(void);
void lapic_send_eoi(void);
