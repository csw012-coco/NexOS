#pragma once

#include <stdint.h>

enum {
    ACPI_MAX_IOAPICS = 8,
    ACPI_MAX_IRQ_OVERRIDES = 24
};

struct acpi_ioapic_route {
    uint8_t id;
    uint8_t reserved[3];
    uint32_t address;
    uint32_t gsi_base;
};

struct acpi_irq_override_route {
    uint8_t bus;
    uint8_t source;
    uint16_t flags;
    uint32_t gsi;
};

struct acpi_status {
    uint8_t present;
    uint8_t revision;
    uint8_t xsdt_present;
    uint8_t rsdt_present;
    uint8_t fadt_present;
    uint8_t madt_present;
    uint8_t hpet_present;
    uint8_t mcfg_present;
    uint8_t lapic_addr_override_present;
    uint8_t reserved[3];
    uint32_t table_count;
    uint32_t lapic_count;
    uint32_t ioapic_count;
    uint32_t irq_override_count;
    uint32_t nmi_count;
    uint32_t lapic_addr;
    uint32_t madt_flags;
    uint32_t rsdp_phys;
    uint32_t rsdt_phys;
    uint64_t xsdt_phys;
    uint64_t lapic_addr_override;
};

int acpi_init(void);
int acpi_poweroff(void);
int acpi_reset(void);
int acpi_query_status(struct acpi_status *out);
uint32_t acpi_ioapic_route_count(void);
uint32_t acpi_irq_override_route_count(void);
int acpi_ioapic_route_at(uint32_t index, struct acpi_ioapic_route *out);
int acpi_irq_override_route_at(uint32_t index,
                               struct acpi_irq_override_route *out);
int acpi_irq_route_for_isa(uint8_t source,
                           struct acpi_irq_override_route *out);
