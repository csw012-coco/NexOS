#include "drivers/bus/acpi.h"

#include <stdint.h>

#include "kernel/public/core/kprint.h"
#include "hal/hal.h"
#include "lib/string.h"

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDP_SCAN_START 0x000e0000u
#define ACPI_RSDP_SCAN_END   0x00100000u
#define ACPI_EBDA_SEG_ADDR   0x0000040eu
#define ACPI_EBDA_SCAN_SIZE  1024u
#define ACPI_TABLE_MAX_COUNT 64u
#define ACPI_DIRECT_READ_LIMIT 0x00100000u
#define ACPI_PAGE_SIZE 4096u
#define ACPI_TEMP_MAP_SLOT 3u

struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_v2 {
    struct acpi_rsdp_v1 v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt_header {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_local_apic {
    struct acpi_madt_entry_header header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry_header header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed));

struct acpi_madt_irq_override {
    struct acpi_madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_lapic_addr_override {
    struct acpi_madt_entry_header header;
    uint16_t reserved;
    uint64_t local_apic_address;
} __attribute__((packed));

enum {
    ACPI_MADT_ENTRY_LOCAL_APIC = 0u,
    ACPI_MADT_ENTRY_IOAPIC = 1u,
    ACPI_MADT_ENTRY_IRQ_OVERRIDE = 2u,
    ACPI_MADT_ENTRY_NMI_SOURCE = 3u,
    ACPI_MADT_ENTRY_LOCAL_APIC_NMI = 4u,
    ACPI_MADT_ENTRY_LOCAL_APIC_ADDR_OVERRIDE = 5u,
    ACPI_MADT_LOCAL_APIC_ENABLED = 1u << 0,
    ACPI_MADT_LOCAL_APIC_ONLINE_CAPABLE = 1u << 1
};

static struct acpi_status g_acpi_status;
static struct acpi_ioapic_route g_acpi_ioapics[ACPI_MAX_IOAPICS];
static struct acpi_irq_override_route g_acpi_irq_overrides[ACPI_MAX_IRQ_OVERRIDES];
static uint32_t g_acpi_ioapic_route_count;
static uint32_t g_acpi_irq_override_route_count;
static uint32_t g_acpi_pm1a_control;
static uint32_t g_acpi_pm1b_control;
static uint32_t g_acpi_smi_command;
static uint8_t g_acpi_enable_command;
static uint16_t g_acpi_sleep_type;
static uint8_t g_acpi_poweroff_ready;
static uint16_t g_acpi_reset_port;
static uint8_t g_acpi_reset_value;
static uint8_t g_acpi_reset_ready;

struct acpi_fadt_legacy {
    struct acpi_sdt_header header;
    uint32_t firmware_control;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_request;
    uint8_t pstate_control;
    uint32_t pm1a_event;
    uint32_t pm1b_event;
    uint32_t pm1a_control;
    uint32_t pm1b_control;
} __attribute__((packed));

static int acpi_memory_equal_local(const char *lhs, const char *rhs, uint32_t size) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < size; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t acpi_read_u16_phys_local(uint32_t phys) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0" : "=r"(value) : "r"((uintptr_t)phys) : "memory");
    return value;
}

static uint8_t acpi_checksum_local(const void *data, uint32_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0u;

    for (uint32_t i = 0u; i < size; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

static int acpi_phys_read_local(uint64_t phys, void *out, uint32_t size) {
    if (out == 0) {
        return 0;
    }
    if (size == 0u) {
        return 1;
    }
    if (phys + size < phys) {
        return 0;
    }
    uint8_t *dest = (uint8_t *)out;

    while (size > 0u) {
        uint64_t page = phys & ~(uint64_t)(ACPI_PAGE_SIZE - 1u);
        uint32_t offset = (uint32_t)(phys & (ACPI_PAGE_SIZE - 1u));
        uint32_t chunk = ACPI_PAGE_SIZE - offset;
        void *mapped = 0;

        if (chunk > size) {
            chunk = size;
        }
        if (phys < ACPI_DIRECT_READ_LIMIT && phys + chunk <= ACPI_DIRECT_READ_LIMIT) {
            memcpy(dest, (const void *)(uintptr_t)phys, chunk);
        } else {
            if (!hal_phys_temporary_map(page, ACPI_TEMP_MAP_SLOT, &mapped)) {
                return 0;
            }
            memcpy(dest, (const uint8_t *)mapped + offset, chunk);
            hal_phys_temporary_unmap(ACPI_TEMP_MAP_SLOT);
        }
        dest += chunk;
        phys += chunk;
        size -= chunk;
    }
    return 1;
}

static int acpi_phys_checksum_local(uint64_t phys, uint32_t size) {
    uint8_t buffer[128];
    uint8_t sum = 0u;

    while (size > 0u) {
        uint32_t chunk = size;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }
        if (!acpi_phys_read_local(phys, buffer, chunk)) {
            return -1;
        }
        for (uint32_t i = 0u; i < chunk; i++) {
            sum = (uint8_t)(sum + buffer[i]);
        }
        phys += chunk;
        size -= chunk;
    }
    return (int)sum;
}

static int acpi_signature_is_local(const char *sig, const char expected[4]) {
    return sig != 0 &&
           sig[0] == expected[0] &&
           sig[1] == expected[1] &&
           sig[2] == expected[2] &&
           sig[3] == expected[3];
}

static int acpi_phys_range_readable_local(uint64_t phys, uint32_t size) {
    if (phys == 0u || size == 0u || phys + size < phys) {
        return 0;
    }
    return hal_paging_enabled();
}

static int acpi_rsdp_signature_is_local(const struct acpi_rsdp_v1 *rsdp) {
    return rsdp != 0 &&
           acpi_memory_equal_local(rsdp->signature, ACPI_RSDP_SIGNATURE, 8u);
}

static int acpi_rsdp_valid_local(const struct acpi_rsdp_v1 *rsdp) {
    const struct acpi_rsdp_v2 *rsdp2;

    if (!acpi_rsdp_signature_is_local(rsdp) ||
        acpi_checksum_local(rsdp, sizeof(*rsdp)) != 0u) {
        return 0;
    }
    if (rsdp->revision < 2u) {
        return 1;
    }
    rsdp2 = (const struct acpi_rsdp_v2 *)rsdp;
    if (rsdp2->length < sizeof(*rsdp2) || rsdp2->length > 4096u) {
        return 0;
    }
    return acpi_checksum_local(rsdp2, rsdp2->length) == 0u;
}

static const struct acpi_rsdp_v1 *acpi_scan_range_local(uint32_t start, uint32_t end) {
    if (end <= start) {
        return 0;
    }
    start = (start + 15u) & ~15u;
    for (uint32_t addr = start; addr + sizeof(struct acpi_rsdp_v1) <= end; addr += 16u) {
        const struct acpi_rsdp_v1 *rsdp =
            (const struct acpi_rsdp_v1 *)(uintptr_t)addr;
        if (acpi_rsdp_valid_local(rsdp)) {
            return rsdp;
        }
    }
    return 0;
}

static const struct acpi_rsdp_v1 *acpi_find_rsdp_local(void) {
    uint16_t ebda_segment = acpi_read_u16_phys_local(ACPI_EBDA_SEG_ADDR);
    uint32_t ebda_base = (uint32_t)ebda_segment << 4;
    const struct acpi_rsdp_v1 *rsdp = 0;

    if (ebda_base >= 0x00080000u && ebda_base < ACPI_RSDP_SCAN_END) {
        rsdp = acpi_scan_range_local(ebda_base, ebda_base + ACPI_EBDA_SCAN_SIZE);
        if (rsdp != 0) {
            return rsdp;
        }
    }
    return acpi_scan_range_local(ACPI_RSDP_SCAN_START, ACPI_RSDP_SCAN_END);
}

static int acpi_sdt_valid_phys_local(uint64_t phys,
                                     struct acpi_sdt_header *header_out) {
    struct acpi_sdt_header header;
    int checksum;

    if (!acpi_phys_range_readable_local(phys, sizeof(header)) ||
        !acpi_phys_read_local(phys, &header, sizeof(header)) ||
        header.length < sizeof(header) ||
        header.length > (1024u * 1024u) ||
        !acpi_phys_range_readable_local(phys, header.length)) {
        return 0;
    }
    checksum = acpi_phys_checksum_local(phys, header.length);
    if (checksum != 0) {
        return 0;
    }
    if (header_out != 0) {
        *header_out = header;
    }
    return 1;
}

static void acpi_reset_routes_local(void) {
    g_acpi_ioapic_route_count = 0u;
    g_acpi_irq_override_route_count = 0u;
    memset(g_acpi_ioapics, 0, sizeof(g_acpi_ioapics));
    memset(g_acpi_irq_overrides, 0, sizeof(g_acpi_irq_overrides));
    g_acpi_pm1a_control = 0u;
    g_acpi_pm1b_control = 0u;
    g_acpi_smi_command = 0u;
    g_acpi_enable_command = 0u;
    g_acpi_sleep_type = 0u;
    g_acpi_poweroff_ready = 0u;
    g_acpi_reset_port = 0u;
    g_acpi_reset_value = 0u;
    g_acpi_reset_ready = 0u;
}

static int acpi_aml_integer_local(uint64_t phys, uint16_t *value_out, uint32_t *size_out) {
    uint8_t opcode;

    if (!acpi_phys_read_local(phys, &opcode, sizeof(opcode)) ||
        value_out == 0 || size_out == 0) {
        return 0;
    }
    if (opcode == 0x0au) {
        uint8_t value;

        if (!acpi_phys_read_local(phys + 1u, &value, sizeof(value))) {
            return 0;
        }
        *value_out = value;
        *size_out = 2u;
        return 1;
    }
    if (opcode == 0x0bu) {
        uint16_t value;

        if (!acpi_phys_read_local(phys + 1u, &value, sizeof(value))) {
            return 0;
        }
        *value_out = value;
        *size_out = 3u;
        return 1;
    }
    if (opcode == 0x00u || opcode == 0x01u) {
        *value_out = opcode;
        *size_out = 1u;
        return 1;
    }
    return 0;
}

static void acpi_find_sleep_type_local(uint64_t dsdt_phys, uint32_t dsdt_length) {
    for (uint32_t offset = sizeof(struct acpi_sdt_header); offset + 10u < dsdt_length; offset++) {
        uint8_t name[6];
        uint16_t type_a;
        uint16_t type_b;
        uint32_t size_a;
        uint32_t size_b;

        if (!acpi_phys_read_local(dsdt_phys + offset, name, sizeof(name)) ||
            name[0] != 0x08u || name[1] != '_' || name[2] != 'S' ||
            name[3] != '5' || name[4] != '_' || name[5] != 0x12u ||
            !acpi_aml_integer_local(dsdt_phys + offset + 8u, &type_a, &size_a) ||
            !acpi_aml_integer_local(dsdt_phys + offset + 8u + size_a, &type_b, &size_b)) {
            continue;
        }
        (void)size_b;
        g_acpi_sleep_type = (uint16_t)(type_a << 10);
        g_acpi_poweroff_ready = 1u;
        return;
    }
}

static uint32_t acpi_gas_io_port_local(const uint8_t gas[12]) {
    uint64_t address = 0u;

    if (gas == 0 || gas[0] != 1u || gas[1] < 16u || gas[2] != 0u) {
        return 0u;
    }
    for (uint32_t i = 0u; i < 8u; i++) {
        address |= (uint64_t)gas[4u + i] << (i * 8u);
    }
    return address <= 0xffffu ? (uint32_t)address : 0u;
}

static void acpi_enable_mode_local(void) {
    if (g_acpi_smi_command == 0u ||
        g_acpi_enable_command == 0u ||
        g_acpi_pm1a_control == 0u ||
        (hal_io_in16((uint16_t)g_acpi_pm1a_control) & 1u) != 0u) {
        return;
    }
    hal_io_out8((uint16_t)g_acpi_smi_command, g_acpi_enable_command);
    for (uint32_t i = 0u; i < 0x100000u; i++) {
        if ((hal_io_in16((uint16_t)g_acpi_pm1a_control) & 1u) != 0u) {
            return;
        }
        __asm__ __volatile__("pause");
    }
}

static void acpi_parse_fadt_local(uint64_t phys, const struct acpi_sdt_header *header) {
    struct acpi_fadt_legacy fadt;
    struct acpi_sdt_header dsdt_header;
    uint8_t reset_register[12];
    uint8_t reset_value;

    if (header == 0 || header->length < sizeof(fadt) ||
        !acpi_phys_read_local(phys, &fadt, sizeof(fadt))) {
        return;
    }
    g_acpi_pm1a_control = fadt.pm1a_control;
    g_acpi_pm1b_control = fadt.pm1b_control;
    g_acpi_smi_command = fadt.smi_command;
    g_acpi_enable_command = fadt.acpi_enable;
    if (header->length >= 204u) {
        uint8_t pm1a_gas[12];
        uint8_t pm1b_gas[12];
        uint32_t address;

        if (acpi_phys_read_local(phys + 180u, pm1a_gas, sizeof(pm1a_gas))) {
            address = acpi_gas_io_port_local(pm1a_gas);
            if (address != 0u) {
                g_acpi_pm1a_control = address;
            }
        }
        if (acpi_phys_read_local(phys + 192u, pm1b_gas, sizeof(pm1b_gas))) {
            address = acpi_gas_io_port_local(pm1b_gas);
            if (address != 0u) {
                g_acpi_pm1b_control = address;
            }
        }
    }
    acpi_enable_mode_local();
    if (header->length >= 131u &&
        acpi_phys_read_local(phys + 116u, reset_register, sizeof(reset_register)) &&
        reset_register[0] == 1u && reset_register[1] >= 8u &&
        reset_register[2] == 0u && reset_register[3] == 1u &&
        acpi_phys_read_local(phys + 128u, &reset_value, sizeof(reset_value)) &&
        reset_register[4] != 0u) {
        uint64_t address = 0u;

        for (uint32_t i = 0u; i < 8u; i++) {
            address |= (uint64_t)reset_register[4u + i] << (i * 8u);
        }
        if (address <= 0xffffu) {
            g_acpi_reset_port = (uint16_t)address;
            g_acpi_reset_value = reset_value;
            g_acpi_reset_ready = 1u;
        }
    }
    if (fadt.dsdt != 0u &&
        acpi_sdt_valid_phys_local(fadt.dsdt, &dsdt_header) &&
        acpi_signature_is_local(dsdt_header.signature, "DSDT")) {
        acpi_find_sleep_type_local(fadt.dsdt, dsdt_header.length);
    }
}

static void acpi_store_ioapic_route_local(const struct acpi_madt_ioapic *ioapic) {
    struct acpi_ioapic_route *route;

    if (ioapic == 0 || g_acpi_ioapic_route_count >= ACPI_MAX_IOAPICS) {
        return;
    }
    route = &g_acpi_ioapics[g_acpi_ioapic_route_count++];
    route->id = ioapic->ioapic_id;
    route->address = ioapic->ioapic_address;
    route->gsi_base = ioapic->global_system_interrupt_base;
}

static void acpi_store_irq_override_route_local(
    const struct acpi_madt_irq_override *override) {
    struct acpi_irq_override_route *route;

    if (override == 0 ||
        g_acpi_irq_override_route_count >= ACPI_MAX_IRQ_OVERRIDES) {
        return;
    }
    route = &g_acpi_irq_overrides[g_acpi_irq_override_route_count++];
    route->bus = override->bus;
    route->source = override->source;
    route->flags = override->flags;
    route->gsi = override->global_system_interrupt;
}

static void acpi_parse_madt_local(uint64_t phys,
                                  const struct acpi_sdt_header *header) {
    struct acpi_madt_header madt;
    uint64_t cursor;
    uint64_t end;

    if (header == 0 || header->length < sizeof(madt)) {
        return;
    }
    if (!acpi_phys_read_local(phys, &madt, sizeof(madt))) {
        return;
    }

    g_acpi_status.lapic_addr = madt.local_apic_address;
    g_acpi_status.madt_flags = madt.flags;
    cursor = phys + sizeof(madt);
    end = phys + header->length;
    while (cursor + sizeof(struct acpi_madt_entry_header) <= end) {
        struct acpi_madt_entry_header entry;

        if (!acpi_phys_read_local(cursor, &entry, sizeof(entry)) ||
            entry.length < sizeof(entry) ||
            cursor + entry.length > end) {
            break;
        }
        switch (entry.type) {
            case ACPI_MADT_ENTRY_LOCAL_APIC: {
                struct acpi_madt_local_apic lapic;

                if (entry.length >= sizeof(lapic) &&
                    acpi_phys_read_local(cursor, &lapic, sizeof(lapic)) &&
                    (lapic.flags & (ACPI_MADT_LOCAL_APIC_ENABLED |
                                    ACPI_MADT_LOCAL_APIC_ONLINE_CAPABLE)) != 0u) {
                    g_acpi_status.lapic_count++;
                }
                break;
            }
            case ACPI_MADT_ENTRY_IOAPIC:
                if (entry.length >= sizeof(struct acpi_madt_ioapic)) {
                    struct acpi_madt_ioapic ioapic;

                    if (acpi_phys_read_local(cursor, &ioapic, sizeof(ioapic))) {
                        acpi_store_ioapic_route_local(&ioapic);
                    }
                    g_acpi_status.ioapic_count++;
                }
                break;
            case ACPI_MADT_ENTRY_IRQ_OVERRIDE:
                if (entry.length >= sizeof(struct acpi_madt_irq_override)) {
                    struct acpi_madt_irq_override override;

                    if (acpi_phys_read_local(cursor, &override, sizeof(override))) {
                        acpi_store_irq_override_route_local(&override);
                    }
                    g_acpi_status.irq_override_count++;
                }
                break;
            case ACPI_MADT_ENTRY_NMI_SOURCE:
            case ACPI_MADT_ENTRY_LOCAL_APIC_NMI:
                g_acpi_status.nmi_count++;
                break;
            case ACPI_MADT_ENTRY_LOCAL_APIC_ADDR_OVERRIDE: {
                struct acpi_madt_lapic_addr_override override;

                if (entry.length >= sizeof(override) &&
                    acpi_phys_read_local(cursor, &override, sizeof(override))) {
                    g_acpi_status.lapic_addr_override_present = 1u;
                    g_acpi_status.lapic_addr_override = override.local_apic_address;
                }
                break;
            }
            default:
                break;
        }
        cursor += entry.length;
    }
}

static void acpi_note_table_phys_local(uint64_t phys) {
    struct acpi_sdt_header table;

    if (!acpi_sdt_valid_phys_local(phys, &table)) {
        return;
    }
    if (acpi_signature_is_local(table.signature, "FACP")) {
        g_acpi_status.fadt_present = 1u;
        acpi_parse_fadt_local(phys, &table);
    } else if (acpi_signature_is_local(table.signature, "APIC")) {
        g_acpi_status.madt_present = 1u;
        acpi_parse_madt_local(phys, &table);
    } else if (acpi_signature_is_local(table.signature, "HPET")) {
        g_acpi_status.hpet_present = 1u;
    } else if (acpi_signature_is_local(table.signature, "MCFG")) {
        g_acpi_status.mcfg_present = 1u;
    }
}

static void acpi_parse_rsdt_local(uint32_t phys) {
    struct acpi_sdt_header rsdt;
    uint32_t count;
    uint64_t entries_phys;

    if (!acpi_sdt_valid_phys_local(phys, &rsdt) ||
        !acpi_signature_is_local(rsdt.signature, "RSDT")) {
        return;
    }
    g_acpi_status.rsdt_present = 1u;
    g_acpi_status.rsdt_phys = phys;
    count = (rsdt.length - sizeof(rsdt)) / sizeof(uint32_t);
    if (count > ACPI_TABLE_MAX_COUNT) {
        count = ACPI_TABLE_MAX_COUNT;
    }
    entries_phys = (uint64_t)phys + sizeof(rsdt);
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t entry = 0u;

        if (acpi_phys_read_local(entries_phys + i * sizeof(entry),
                                 &entry,
                                 sizeof(entry)) &&
            entry != 0u) {
            acpi_note_table_phys_local(entry);
        }
    }
    if (g_acpi_status.table_count == 0u) {
        g_acpi_status.table_count = count;
    }
}

static void acpi_parse_xsdt_local(uint64_t phys) {
    struct acpi_sdt_header xsdt;
    uint32_t count;
    uint64_t entries_phys;

    if (!acpi_sdt_valid_phys_local(phys, &xsdt) ||
        !acpi_signature_is_local(xsdt.signature, "XSDT")) {
        return;
    }
    g_acpi_status.xsdt_present = 1u;
    g_acpi_status.xsdt_phys = phys;
    count = (xsdt.length - sizeof(xsdt)) / sizeof(uint64_t);
    if (count > ACPI_TABLE_MAX_COUNT) {
        count = ACPI_TABLE_MAX_COUNT;
    }
    entries_phys = phys + sizeof(xsdt);
    for (uint32_t i = 0u; i < count; i++) {
        uint64_t entry = 0u;

        if (acpi_phys_read_local(entries_phys + i * sizeof(entry),
                                 &entry,
                                 sizeof(entry)) &&
            entry != 0u) {
            acpi_note_table_phys_local(entry);
        }
    }
    g_acpi_status.table_count = count;
}

int acpi_init(void) {
    const struct acpi_rsdp_v1 *rsdp;

    memset(&g_acpi_status, 0, sizeof(g_acpi_status));
    acpi_reset_routes_local();
    rsdp = acpi_find_rsdp_local();
    if (rsdp == 0) {
        return 0;
    }

    g_acpi_status.present = 1u;
    g_acpi_status.revision = rsdp->revision;
    g_acpi_status.rsdp_phys = (uint32_t)(uintptr_t)rsdp;
    if (rsdp->revision >= 2u) {
        const struct acpi_rsdp_v2 *rsdp2 = (const struct acpi_rsdp_v2 *)rsdp;
        acpi_parse_xsdt_local(rsdp2->xsdt_address);
    }
    acpi_parse_rsdt_local(rsdp->rsdt_address);

    kprint("acpi: rsdp=%x rev=%u rsdt=%x xsdt=%lx tables=%u fadt=%u madt=%u hpet=%u mcfg=%u\n",
           g_acpi_status.rsdp_phys,
           (uint32_t)g_acpi_status.revision,
           g_acpi_status.rsdt_phys,
           g_acpi_status.xsdt_phys,
           g_acpi_status.table_count,
           (uint32_t)g_acpi_status.fadt_present,
           (uint32_t)g_acpi_status.madt_present,
           (uint32_t)g_acpi_status.hpet_present,
           (uint32_t)g_acpi_status.mcfg_present);
    if (g_acpi_status.madt_present) {
        kprint("acpi: madt lapic=%x flags=%x cpus=%u ioapic=%u irq_override=%u nmi=%u lapic64=%lx\n",
               g_acpi_status.lapic_addr,
               g_acpi_status.madt_flags,
               g_acpi_status.lapic_count,
               g_acpi_status.ioapic_count,
               g_acpi_status.irq_override_count,
               g_acpi_status.nmi_count,
               g_acpi_status.lapic_addr_override_present
                   ? g_acpi_status.lapic_addr_override
                   : 0ull);
    }
    return 1;
}

int acpi_poweroff(void) {
    uint16_t sleep_command;

    if (!g_acpi_poweroff_ready || g_acpi_pm1a_control == 0u) {
        return 0;
    }
    sleep_command = (uint16_t)(g_acpi_sleep_type | (1u << 13));
    hal_io_out16((uint16_t)g_acpi_pm1a_control, sleep_command);
    if (g_acpi_pm1b_control != 0u) {
        hal_io_out16((uint16_t)g_acpi_pm1b_control, sleep_command);
    }
    for (;;) {
        hal_cpu_halt();
    }
}

int acpi_reset(void) {
    if (!g_acpi_reset_ready) {
        return 0;
    }
    hal_io_out8(g_acpi_reset_port, g_acpi_reset_value);
    return 1;
}

int acpi_query_status(struct acpi_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_acpi_status;
    return g_acpi_status.present != 0u;
}

uint32_t acpi_ioapic_route_count(void) {
    return g_acpi_ioapic_route_count;
}

uint32_t acpi_irq_override_route_count(void) {
    return g_acpi_irq_override_route_count;
}

int acpi_ioapic_route_at(uint32_t index, struct acpi_ioapic_route *out) {
    if (out == 0 || index >= g_acpi_ioapic_route_count) {
        return 0;
    }
    *out = g_acpi_ioapics[index];
    return 1;
}

int acpi_irq_override_route_at(uint32_t index,
                               struct acpi_irq_override_route *out) {
    if (out == 0 || index >= g_acpi_irq_override_route_count) {
        return 0;
    }
    *out = g_acpi_irq_overrides[index];
    return 1;
}

int acpi_irq_route_for_isa(uint8_t source,
                           struct acpi_irq_override_route *out) {
    for (uint32_t i = 0u; i < g_acpi_irq_override_route_count; i++) {
        if (g_acpi_irq_overrides[i].bus == 0u &&
            g_acpi_irq_overrides[i].source == source) {
            if (out != 0) {
                *out = g_acpi_irq_overrides[i];
            }
            return 1;
        }
    }
    if (out != 0) {
        memset(out, 0, sizeof(*out));
        out->source = source;
        out->gsi = source;
    }
    return 0;
}
