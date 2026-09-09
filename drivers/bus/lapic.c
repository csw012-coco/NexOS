#include "drivers/bus/lapic.h"

#include "drivers/bus/acpi.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "lib/string.h"

enum {
    LAPIC_DEFAULT_BASE = 0xfee00000u,
    LAPIC_MMIO_SIZE = 0x1000u,
    LAPIC_CPUID_FEATURE_APIC = 1u << 9,
    LAPIC_MSR_APIC_BASE = 0x1bu,
    LAPIC_MSR_APIC_BASE_ENABLE = 1u << 11,
    LAPIC_ID_OFFSET = 0x020u,
    LAPIC_TPR_OFFSET = 0x080u,
    LAPIC_EOI_OFFSET = 0x0b0u,
    LAPIC_SVR_OFFSET = 0x0f0u,
    LAPIC_SVR_SOFTWARE_ENABLE = 1u << 8,
    LAPIC_SPURIOUS_VECTOR = 0x2fu
};

static volatile uint32_t *g_lapic_mmio;
static struct lapic_status g_lapic_status;

static uint64_t lapic_read_msr_local(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void lapic_write_msr_local(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);

    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static int lapic_cpu_supported_local(void) {
    uint32_t eax = 0u;
    uint32_t edx = 0u;

    hal_cpu_cpuid(1u, 0u, &eax, 0, 0, &edx);
    (void)eax;
    return (edx & LAPIC_CPUID_FEATURE_APIC) != 0u;
}

static void lapic_write_local(uint32_t offset, uint32_t value) {
    if (g_lapic_mmio == 0 || offset >= LAPIC_MMIO_SIZE) {
        return;
    }
    g_lapic_mmio[offset / 4u] = value;
}

static uint32_t lapic_read_local(uint32_t offset) {
    if (g_lapic_mmio == 0 || offset >= LAPIC_MMIO_SIZE) {
        return 0;
    }
    return g_lapic_mmio[offset / 4u];
}

static uint64_t lapic_address_from_acpi_local(void) {
    struct acpi_status acpi;

    if (acpi_query_status(&acpi) && acpi.madt_present) {
        if (acpi.lapic_addr_override_present && acpi.lapic_addr_override != 0u) {
            return acpi.lapic_addr_override;
        }
        if (acpi.lapic_addr != 0u) {
            return acpi.lapic_addr;
        }
    }
    return LAPIC_DEFAULT_BASE;
}

int lapic_init_from_acpi(void) {
    uint64_t address;

    memset(&g_lapic_status, 0, sizeof(g_lapic_status));
    g_lapic_mmio = 0;
    address = lapic_address_from_acpi_local();
    if (address == 0u) {
        return 0;
    }
    g_lapic_mmio = (volatile uint32_t *)hal_mmio_map(address, LAPIC_MMIO_SIZE);
    if (g_lapic_mmio == 0) {
        g_lapic_status.address = address;
        return 0;
    }
    g_lapic_status.present = 1u;
    g_lapic_status.mapped = 1u;
    g_lapic_status.address = address;
    g_lapic_status.id = (uint8_t)(lapic_read_local(LAPIC_ID_OFFSET) >> 24);
    kprint("lapic: addr=%lx mapped=1 enabled=0\n", address);
    return 1;
}

int lapic_enable(void) {
    uint64_t apic_base;
    uint32_t svr;

    if (!g_lapic_status.mapped || g_lapic_mmio == 0) {
        return 0;
    }
    if (!lapic_cpu_supported_local()) {
        kprint("lapic: enable skipped apic feature missing\n");
        return 0;
    }
    apic_base = lapic_read_msr_local(LAPIC_MSR_APIC_BASE);
    if ((apic_base & LAPIC_MSR_APIC_BASE_ENABLE) == 0u) {
        lapic_write_msr_local(LAPIC_MSR_APIC_BASE,
                              apic_base | LAPIC_MSR_APIC_BASE_ENABLE);
    }
    g_lapic_status.msr_enabled = 1u;
    lapic_write_local(LAPIC_TPR_OFFSET, 0u);
    svr = lapic_read_local(LAPIC_SVR_OFFSET);
    svr &= ~0xffu;
    svr |= LAPIC_SPURIOUS_VECTOR | LAPIC_SVR_SOFTWARE_ENABLE;
    lapic_write_local(LAPIC_SVR_OFFSET, svr);
    g_lapic_status.enabled = 1u;
    g_lapic_status.id = (uint8_t)(lapic_read_local(LAPIC_ID_OFFSET) >> 24);
    kprint("lapic: enabled id=%u spurious=%x msr=1\n",
           (uint32_t)g_lapic_status.id,
           LAPIC_SPURIOUS_VECTOR);
    return 1;
}

int lapic_query_status(struct lapic_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_lapic_status;
    return g_lapic_status.present != 0u;
}

uint8_t lapic_current_id(void) {
    if (g_lapic_status.mapped && g_lapic_mmio != 0) {
        g_lapic_status.id = (uint8_t)(lapic_read_local(LAPIC_ID_OFFSET) >> 24);
    }
    return g_lapic_status.id;
}

void lapic_send_eoi(void) {
    lapic_write_local(LAPIC_EOI_OFFSET, 0u);
}
