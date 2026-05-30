#include "drivers/usb/xhci_internal.h"

static int xhci_driver_init_local(void) {
    xhci_init();
    return 1;
}

const struct kernel_driver xhci_kernel_driver = {
    .name = "XHCI",
    .kind = KERNEL_DRIVER_KIND_USB,
    .init = xhci_driver_init_local,
    .exit = NULL,
};

static int xhci_init_controller(uint8_t controller_index, const struct pci_xhci_controller *xhci) {
    uint64_t mmio;
    uint32_t hcs1;
    uint32_t hcs2;
    uint32_t hcc1;
    uint32_t dboff;
    uint32_t rtsoff;
    volatile uint8_t *intr0;
    uint32_t scratchpads;

    memset(&g_xhci, 0, sizeof(g_xhci));
    g_xhci_active_controller = controller_index;

    pci_config_write16(xhci->bus, xhci->slot, xhci->function, 0x04,
                       (uint16_t)(pci_config_read16(xhci->bus, xhci->slot, xhci->function, 0x04) | 0x0007u));
    mmio = ((uint64_t)(xhci->mmio_base_hi & 0xffffffffu) << 32) | (uint64_t)(xhci->mmio_base_lo & 0xfffffff0u);
    if (mmio == 0u) {
        kprint("xhci: unsupported mmio=%lx\n", mmio);
        return 0;
    }
    g_xhci.cap = (volatile uint8_t *)hal_mmio_map(mmio, XHCI_MMIO_MAP_SIZE);
    if (g_xhci.cap == 0) {
        kprint("xhci: mmio map failed mmio=%lx\n", mmio);
        return 0;
    }

    g_xhci.cap_length = (uint8_t)(xhci_read32(g_xhci.cap, 0x00u) & 0xffu);
    g_xhci.op = g_xhci.cap + g_xhci.cap_length;
    hcs1 = xhci_read32(g_xhci.cap, 0x04u);
    hcs2 = xhci_read32(g_xhci.cap, 0x08u);
    hcc1 = xhci_read32(g_xhci.cap, 0x10u);
    dboff = xhci_read32(g_xhci.cap, 0x14u) & ~0x3u;
    rtsoff = xhci_read32(g_xhci.cap, 0x18u) & ~0x1fu;
    g_xhci.doorbell = g_xhci.cap + dboff;
    g_xhci.runtime = g_xhci.cap + rtsoff;
    g_xhci.max_slots = (uint8_t)(hcs1 & 0xffu);
    g_xhci.max_intrs = (uint16_t)((hcs1 >> 8) & 0x7ffu);
    g_xhci.max_ports = (uint8_t)((hcs1 >> 24) & 0xffu);
    g_xhci.page_size = xhci_read32(g_xhci.op, XHCI_OP_PAGESIZE);
    scratchpads = xhci_max_scratchpads(hcs2);

    xhci_log_supported_protocols(hcc1);
    xhci_bios_handoff(hcc1);
    xhci_write32(g_xhci.op, XHCI_OP_USBCMD, xhci_read32(g_xhci.op, XHCI_OP_USBCMD) & ~XHCI_USBCMD_RS);
    (void)xhci_wait_set(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH);
    xhci_write32(g_xhci.op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST);
    if (!xhci_wait_clear(g_xhci.op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST) ||
        !xhci_wait_clear(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_CNR)) {
        kprint("xhci: reset timeout\n");
        return 0;
    }
    if (!xhci_alloc_core_rings(scratchpads)) {
        kprint("xhci: dma allocation failed\n");
        return 0;
    }
    g_xhci.command_enqueue = 0u;
    g_xhci.command_cycle = 1u;
    g_xhci.event_dequeue = 0u;
    g_xhci.event_cycle = 1u;
    g_xhci.context_size = (hcc1 & (1u << 2)) != 0u ? 64u : 32u;

    xhci_write64(g_xhci.op, XHCI_OP_DCBAAP, g_xhci.dcbaa_phys);
    xhci_write64(g_xhci.op, XHCI_OP_CRCR, g_xhci.command_ring_phys | 1u);
    xhci_write32(g_xhci.op, XHCI_OP_CONFIG, g_xhci.max_slots);
    xhci_write32(g_xhci.op, XHCI_OP_DNCTRL, 0u);

    intr0 = g_xhci.runtime + 0x20u;
    xhci_write32(intr0, XHCI_INTR_ERSTSZ, XHCI_ERST_ENTRIES);
    xhci_write64(intr0, XHCI_INTR_ERSTBA, g_xhci.erst_phys);
    xhci_write64(intr0, XHCI_INTR_ERDP, g_xhci.event_ring_phys);
    xhci_write32(intr0, XHCI_INTR_IMOD, 0u);
    xhci_write32(intr0, XHCI_INTR_IMAN, 3u);

    xhci_write32(g_xhci.op, XHCI_OP_USBCMD, XHCI_USBCMD_RS | XHCI_USBCMD_INTE);
    if (!xhci_wait_clear(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH)) {
        kprint("xhci: run timeout\n");
        return 0;
    }
    xhci_power_root_ports();
    xhci_log_root_ports(controller_index, "powered");

    for (uint32_t port = 1; port <= g_xhci.max_ports; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE);

        if ((portsc & XHCI_PORTSC_CCS) != 0u) {
            uint32_t speed = (portsc >> 10) & 0x0fu;

            g_xhci.connected_ports++;
            kprint("xhci: port%u connected speed=%u enabled=%u powered=%u portsc=%x\n",
                   port,
                   speed,
                   (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
                   (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
                   portsc);
        }
    }
    xhci_enumerate_connected_ports();
    g_xhci.initialized = 1u;
    xhci_save_active_controller();
    kprint("xhci%u: controller bdf=%u:%u.%u mmio=%lx slots=%u intrs=%u ports=%u ctx=%u scratch=%u hcc=%x rev=msc-v2\n",
           (uint32_t)controller_index,
           (uint32_t)xhci->bus,
           (uint32_t)xhci->slot,
           (uint32_t)xhci->function,
           mmio,
           (uint32_t)g_xhci.max_slots,
           (uint32_t)g_xhci.max_intrs,
           (uint32_t)g_xhci.max_ports,
           (uint32_t)g_xhci.context_size,
           scratchpads,
           hcc1);
    return 1;
}

void xhci_init(void) {
    struct pci_xhci_controller xhci;

    memset(&g_xhci, 0, sizeof(g_xhci));
    memset(g_xhci_controllers, 0, sizeof(g_xhci_controllers));
    g_xhci_controller_count = 0u;
    g_xhci_active_controller = 0u;
    g_xhci_msc_count = 0u;
    g_hid_keyboard_count = 0u;
    g_hid_mouse_count = 0u;
    memset(g_hid_mice, 0, sizeof(g_hid_mice));
    g_hid_event_head = 0u;
    g_hid_event_tail = 0u;
    g_hid_event_count = 0u;
    g_hid_last_repeat_tick = 0u;
    g_xhci_last_hotplug_tick = 0u;
    g_xhci_busy = 0u;
    for (uint32_t index = 0u; index < XHCI_MAX_CONTROLLERS && pci_find_xhci_controller_at(index, &xhci); index++) {
        if (xhci_init_controller((uint8_t)index, &xhci)) {
            g_xhci_controller_count = index + 1u;
        }
    }
    if (g_xhci_controller_count != 0u) {
        (void)xhci_select_controller(0u);
    }
}

uint32_t xhci_port_count(void) {
    uint32_t total = 0u;

    for (uint32_t i = 0u; i < g_xhci_controller_count && i < XHCI_MAX_CONTROLLERS; i++) {
        if (g_xhci_controllers[i].initialized) {
            total += g_xhci_controllers[i].max_ports;
        }
    }
    return total;
}

uint32_t xhci_connected_port_count(void) {
    uint32_t total = 0u;

    for (uint32_t i = 0u; i < g_xhci_controller_count && i < XHCI_MAX_CONTROLLERS; i++) {
        if (g_xhci_controllers[i].initialized) {
            total += g_xhci_controllers[i].connected_ports;
        }
    }
    return total;
}
