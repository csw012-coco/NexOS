#include "drivers/bus/ioapic.h"

#include "drivers/bus/acpi.h"
#include "drivers/bus/lapic.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "lib/string.h"

enum {
    IOAPIC_REG_SELECT = 0x00u,
    IOAPIC_REG_WINDOW = 0x10u,
    IOAPIC_REG_ID = 0x00u,
    IOAPIC_REG_VERSION = 0x01u,
    IOAPIC_REG_REDIR_BASE = 0x10u,
    IOAPIC_REDIR_MASKED = 1u << 16,
    IOAPIC_REDIR_TRIGGER_LEVEL = 1u << 15,
    IOAPIC_REDIR_POLARITY_LOW = 1u << 13,
    IOAPIC_IRQ_VECTOR_BASE = 0x20u
};

static volatile uint32_t *g_ioapic_mmio;
static struct ioapic_status g_ioapic_status;

static uint32_t ioapic_read_reg_local(uint32_t reg) {
    if (g_ioapic_mmio == 0) {
        return 0;
    }
    g_ioapic_mmio[IOAPIC_REG_SELECT / 4u] = reg;
    return g_ioapic_mmio[IOAPIC_REG_WINDOW / 4u];
}

static void ioapic_write_reg_local(uint32_t reg, uint32_t value) {
    if (g_ioapic_mmio == 0) {
        return;
    }
    g_ioapic_mmio[IOAPIC_REG_SELECT / 4u] = reg;
    g_ioapic_mmio[IOAPIC_REG_WINDOW / 4u] = value;
}

static uint32_t ioapic_redir_low_from_flags_local(uint8_t irq,
                                                  const struct acpi_irq_override_route *route,
                                                  int masked) {
    uint32_t low = IOAPIC_IRQ_VECTOR_BASE + irq;
    uint16_t flags = route != 0 ? route->flags : 0u;
    uint16_t polarity = flags & 0x3u;
    uint16_t trigger = flags & 0xcu;

    if (masked) {
        low |= IOAPIC_REDIR_MASKED;
    }
    if (polarity == 0x3u) {
        low |= IOAPIC_REDIR_POLARITY_LOW;
    }
    if (trigger == 0xcu) {
        low |= IOAPIC_REDIR_TRIGGER_LEVEL;
    }
    return low;
}

static int ioapic_route_index_local(uint8_t irq, uint32_t *index_out) {
    struct acpi_irq_override_route route;
    uint32_t gsi;

    if (index_out == 0 || irq >= 16u || !g_ioapic_status.present) {
        return 0;
    }
    if (acpi_irq_route_for_isa(irq, &route)) {
        gsi = route.gsi;
    } else {
        gsi = irq;
    }
    if (gsi < g_ioapic_status.gsi_base) {
        return 0;
    }
    gsi -= g_ioapic_status.gsi_base;
    if (gsi >= g_ioapic_status.redirection_entries) {
        return 0;
    }
    *index_out = gsi;
    return 1;
}

static uint32_t ioapic_gsi_for_irq_local(uint8_t irq) {
    struct acpi_irq_override_route route;

    if (acpi_irq_route_for_isa(irq, &route)) {
        return route.gsi;
    }
    return irq;
}

static int ioapic_program_route_local(uint8_t irq, int masked) {
    struct acpi_irq_override_route route;
    struct acpi_irq_override_route *route_ptr = 0;
    uint32_t index;
    uint32_t low;
    uint32_t high;

    if (!ioapic_route_index_local(irq, &index)) {
        return 0;
    }
    if (acpi_irq_route_for_isa(irq, &route)) {
        route_ptr = &route;
    }
    low = ioapic_redir_low_from_flags_local(irq, route_ptr, masked);
    high = (uint32_t)lapic_current_id() << 24;
    ioapic_write_reg_local(IOAPIC_REG_REDIR_BASE + index * 2u + 1u, high);
    ioapic_write_reg_local(IOAPIC_REG_REDIR_BASE + index * 2u, low);
    return 1;
}

static void ioapic_program_masked_route_local(uint8_t irq) {
    if (ioapic_program_route_local(irq, 1)) {
        g_ioapic_status.route_count++;
    }
}

static int ioapic_token_matches_local(const char *token,
                                      const char *name,
                                      uint32_t name_len) {
    for (uint32_t i = 0u; i < name_len; i++) {
        if (token[i] != name[i]) {
            return 0;
        }
    }
    return token[name_len] == '\0' || token[name_len] == ' ';
}

static int ioapic_parse_irq_value_local(const char *text, uint8_t *irq_out) {
    uint32_t value = 0u;
    uint32_t digits = 0u;

    if (text == 0 || irq_out == 0) {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text - '0');
        digits++;
        text++;
    }
    if (digits == 0u || value >= 16u) {
        return 0;
    }
    if (*text != '\0' && *text != ' ') {
        return 0;
    }
    *irq_out = (uint8_t)value;
    return 1;
}

int ioapic_init_from_acpi(void) {
    struct acpi_ioapic_route ioapic;
    uint32_t version;

    memset(&g_ioapic_status, 0, sizeof(g_ioapic_status));
    g_ioapic_mmio = 0;
    if (!acpi_ioapic_route_at(0u, &ioapic) || ioapic.address == 0u) {
        return 0;
    }
    g_ioapic_mmio = (volatile uint32_t *)hal_mmio_map(ioapic.address, 0x1000u);
    if (g_ioapic_mmio == 0) {
        return 0;
    }

    g_ioapic_status.present = 1u;
    g_ioapic_status.address = ioapic.address;
    g_ioapic_status.gsi_base = ioapic.gsi_base;
    g_ioapic_status.id = (ioapic_read_reg_local(IOAPIC_REG_ID) >> 24) & 0xffu;
    version = ioapic_read_reg_local(IOAPIC_REG_VERSION);
    g_ioapic_status.redirection_entries = ((version >> 16) & 0xffu) + 1u;

    for (uint8_t irq = 0u; irq < 16u; irq++) {
        ioapic_program_masked_route_local(irq);
    }
    g_ioapic_status.configured = g_ioapic_status.route_count != 0u ? 1u : 0u;
    kprint("ioapic: id=%u addr=%x gsi_base=%u redir=%u routes=%u mode=masked\n",
           g_ioapic_status.id,
           g_ioapic_status.address,
           g_ioapic_status.gsi_base,
           g_ioapic_status.redirection_entries,
           (uint32_t)g_ioapic_status.route_count);
    return g_ioapic_status.configured != 0u;
}

int ioapic_set_irq_mask(uint8_t irq, int masked) {
    if (irq >= 16u || !g_ioapic_status.configured) {
        return 0;
    }
    return ioapic_program_route_local(irq, masked);
}

int ioapic_enable_irq(uint8_t irq) {
    if (irq >= 16u) {
        return 0;
    }
    if ((g_ioapic_status.enabled_irq_mask & (1u << irq)) == 0u) {
        g_ioapic_status.enabled_irq_mask |= 1u << irq;
        g_ioapic_status.enabled_count++;
    }
    if (!ioapic_set_irq_mask(irq, 0)) {
        g_ioapic_status.enabled_irq_mask &= ~(1u << irq);
        if (g_ioapic_status.enabled_count != 0u) {
            g_ioapic_status.enabled_count--;
        }
        return 0;
    }
    kprint("ioapic: irq%u unmasked gsi=%u\n",
           (uint32_t)irq,
           ioapic_gsi_for_irq_local(irq));
    return 1;
}

int ioapic_irq_enabled(uint8_t irq) {
    if (irq >= 16u) {
        return 0;
    }
    return (g_ioapic_status.enabled_irq_mask & (1u << irq)) != 0u;
}

uint32_t ioapic_enabled_irq_mask(void) {
    return g_ioapic_status.enabled_irq_mask;
}

int ioapic_configure_from_cmdline(const char *cmdline) {
    int enabled = 0;

    while (cmdline != 0 && *cmdline != '\0') {
        uint8_t irq = 0u;

        cmdline = skip_spaces(cmdline);
        if (*cmdline == '\0') {
            break;
        }
        if (ioapic_token_matches_local(cmdline, "ioapic.keyboard=1", 17u) ||
            ioapic_token_matches_local(cmdline, "ioapic=keyboard", 15u)) {
            enabled += ioapic_enable_irq(1u);
        } else if (starts_with(cmdline, "ioapic.irq=") &&
                   ioapic_parse_irq_value_local(cmdline + 11, &irq)) {
            enabled += ioapic_enable_irq(irq);
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    if (enabled != 0) {
        kprint("ioapic: enabled_mask=%x\n", g_ioapic_status.enabled_irq_mask);
    }
    return enabled;
}

int ioapic_query_status(struct ioapic_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_ioapic_status;
    return g_ioapic_status.present != 0u;
}
