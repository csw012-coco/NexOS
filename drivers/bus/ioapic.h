#pragma once

#include <stdint.h>

struct ioapic_status {
    uint8_t present;
    uint8_t configured;
    uint8_t route_count;
    uint8_t enabled_count;
    uint32_t id;
    uint32_t address;
    uint32_t gsi_base;
    uint32_t redirection_entries;
    uint32_t enabled_irq_mask;
};

int ioapic_init_from_acpi(void);
int ioapic_enable_irq(uint8_t irq);
int ioapic_set_irq_mask(uint8_t irq, int masked);
int ioapic_irq_enabled(uint8_t irq);
uint32_t ioapic_enabled_irq_mask(void);
int ioapic_configure_from_cmdline(const char *cmdline);
int ioapic_enable_isa_default_irqs(void);
int ioapic_enable_all_isa_irqs(void);
int ioapic_query_status(struct ioapic_status *out);
