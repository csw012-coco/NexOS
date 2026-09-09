#include "drivers/usb/xhci_internal.h"

struct xhci_state g_xhci;
struct xhci_state g_xhci_controllers[XHCI_MAX_CONTROLLERS];
uint32_t g_xhci_controller_count;
uint8_t g_xhci_active_controller;
struct xhci_enum_device g_enum_devices[XHCI_MAX_ENUM_DEVICES];
uint32_t g_xhci_msc_count;
struct xhci_hid_keyboard g_hid_keyboards[XHCI_MAX_HID_KEYBOARDS];
uint32_t g_hid_keyboard_count;
struct xhci_hid_keyboard g_hid_mice[XHCI_MAX_HID_KEYBOARDS];
uint32_t g_hid_mouse_count;
struct keyboard_event g_hid_event_queue[XHCI_HID_EVENT_QUEUE_SIZE];
uint32_t g_hid_event_head;
uint32_t g_hid_event_tail;
uint32_t g_hid_event_count;
uint32_t g_hid_last_repeat_tick;
uint32_t g_xhci_last_hotplug_tick;
volatile uint32_t g_xhci_busy;

int xhci_try_begin_busy(void) {
    return __sync_lock_test_and_set(&g_xhci_busy, 1u) == 0u;
}

void xhci_end_busy(void) {
    __sync_lock_release(&g_xhci_busy);
}

void xhci_save_active_controller(void) {
    if (g_xhci_active_controller < XHCI_MAX_CONTROLLERS) {
        g_xhci_controllers[g_xhci_active_controller] = g_xhci;
    }
}

int xhci_select_controller(uint8_t index) {
    if (index == g_xhci_active_controller && g_xhci.cap != 0) {
        return 1;
    }
    if (index >= g_xhci_controller_count || index >= XHCI_MAX_CONTROLLERS ||
        !g_xhci_controllers[index].initialized) {
        return 0;
    }
    if (g_xhci_active_controller != index) {
        xhci_save_active_controller();
        g_xhci = g_xhci_controllers[index];
        g_xhci_active_controller = index;
    }
    return 1;
}

uint32_t xhci_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

void xhci_write32(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

void xhci_write64(volatile uint8_t *base, uint32_t offset, uint64_t value) {
    xhci_write32(base, offset, (uint32_t)value);
    xhci_write32(base, offset + 4u, (uint32_t)(value >> 32));
}

int xhci_wait_clear(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((xhci_read32(base, offset) & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

int xhci_wait_set(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((xhci_read32(base, offset) & mask) != 0u) {
            return 1;
        }
    }
    return 0;
}

uint32_t xhci_max_scratchpads(uint32_t hcsparams2) {
    uint32_t hi = (hcsparams2 >> 21) & 0x1fu;
    uint32_t lo = (hcsparams2 >> 27) & 0x1fu;

    return (hi << 5) | lo;
}

uint8_t *xhci_context_ptr(uint8_t *base, uint32_t index) {
    return base + index * g_xhci.context_size;
}

void xhci_ring_trb_set(struct xhci_trb *trb,
                              uint64_t parameter,
                              uint32_t status,
                              uint32_t control) {
    trb->parameter_lo = (uint32_t)parameter;
    trb->parameter_hi = (uint32_t)(parameter >> 32);
    trb->status = status;
    trb->control = control;
}

uint32_t xhci_trb_type(uint32_t control) {
    return (control >> 10) & 0x3fu;
}

void xhci_delay_spin(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

void xhci_delay_ms(uint32_t ms) {
    uint32_t hz = hal_timer_hz();
    uint32_t start;
    uint32_t last;
    uint32_t stagnant_spins = 0u;
    uint64_t wait_ticks;

    if (ms == 0u) {
        return;
    }
    if (hz == 0u) {
        while (ms-- != 0u) {
            xhci_delay_spin(100000u);
        }
        return;
    }
    wait_ticks = ((uint64_t)hz * (uint64_t)ms + 999u) / 1000u;
    if (wait_ticks == 0u) {
        wait_ticks = 1u;
    }
    if (wait_ticks > 0xffffffffull) {
        wait_ticks = 0xffffffffu;
    }
    start = hal_timer_current_ticks();
    last = start;
    while ((uint32_t)(hal_timer_current_ticks() - start) < (uint32_t)wait_ticks) {
        uint32_t now = hal_timer_current_ticks();

        if (now != last) {
            last = now;
            stagnant_spins = 0u;
        } else {
            stagnant_spins++;
            if (stagnant_spins >= 1000000u) {
                break;
            }
        }
        hal_cpu_relax();
    }
    if ((uint32_t)(hal_timer_current_ticks() - start) < (uint32_t)wait_ticks) {
        while (ms-- != 0u) {
            xhci_delay_spin(100000u);
        }
    }
}

void xhci_bios_handoff(uint32_t hccparams1) {
    uint32_t ext_offset = ((hccparams1 >> 16) & 0xffffu) * 4u;
    uint32_t guard = 64u;

    while (ext_offset != 0u && guard-- != 0u) {
        uint32_t cap = xhci_read32(g_xhci.cap, ext_offset);
        uint32_t cap_id = cap & 0xffu;
        uint32_t next = ((cap >> 8) & 0xffu) * 4u;

        if (cap_id == XHCI_EXT_CAP_LEGACY_SUPPORT) {
            if ((cap & XHCI_LEGACY_BIOS_OWNED) != 0u) {
                xhci_write32(g_xhci.cap, ext_offset, cap | XHCI_LEGACY_OS_OWNED);
                for (uint32_t i = 0u; i < 100u; i++) {
                    xhci_delay_ms(10u);
                    cap = xhci_read32(g_xhci.cap, ext_offset);
                    if ((cap & XHCI_LEGACY_BIOS_OWNED) == 0u) {
                        break;
                    }
                }
                if ((cap & XHCI_LEGACY_BIOS_OWNED) != 0u) {
                    xhci_write32(g_xhci.cap, ext_offset,
                                 (cap | XHCI_LEGACY_OS_OWNED) & ~XHCI_LEGACY_BIOS_OWNED);
                }
            } else if ((cap & XHCI_LEGACY_OS_OWNED) == 0u) {
                xhci_write32(g_xhci.cap, ext_offset, cap | XHCI_LEGACY_OS_OWNED);
            }
            xhci_write32(g_xhci.cap, ext_offset + 4u, 0u);
        }
        if (next == 0u) {
            break;
        }
        ext_offset += next;
    }
}

void xhci_log_supported_protocols(uint32_t hccparams1) {
    uint32_t ext_offset = ((hccparams1 >> 16) & 0xffffu) * 4u;
    uint32_t guard = 64u;

    while (ext_offset != 0u && guard-- != 0u) {
        uint32_t cap = xhci_read32(g_xhci.cap, ext_offset);
        uint32_t cap_id = cap & 0xffu;
        uint32_t next = ((cap >> 8) & 0xffu) * 4u;

        if (cap_id == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
            uint32_t name = xhci_read32(g_xhci.cap, ext_offset + 4u);
            uint32_t ports = xhci_read32(g_xhci.cap, ext_offset + 8u);
            uint32_t port_offset = ports & 0xffu;
            uint32_t port_count = (ports >> 8) & 0xffu;
            char proto[5];

            proto[0] = (char)(name & 0xffu);
            proto[1] = (char)((name >> 8) & 0xffu);
            proto[2] = (char)((name >> 16) & 0xffu);
            proto[3] = (char)((name >> 24) & 0xffu);
            proto[4] = '\0';
            XHCI_ENUM_TRACE("xhci: protocol %s rev=%u.%u ports=%u..%u raw=%x\n",
                            proto,
                            (cap >> 24) & 0xffu,
                            (cap >> 16) & 0xffu,
                            port_offset,
                            port_count != 0u ? port_offset + port_count - 1u : 0u,
                            ports);
        }
        if (next == 0u) {
            break;
        }
        ext_offset += next;
    }
}

void xhci_power_root_ports(void) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports; port++) {
        uint32_t offset = XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
        uint32_t portsc = xhci_read32(g_xhci.op, offset);

        if ((portsc & XHCI_PORTSC_PP) == 0u) {
            xhci_write32(g_xhci.op, offset, (portsc & XHCI_PORTSC_RW_PRESERVE) | XHCI_PORTSC_PP);
        }
    }
    xhci_delay_ms(100u);
}

void xhci_log_root_ports(uint8_t controller_index, const char *phase) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE);

        if (!XHCI_VERBOSE_ROOT_PORTS && (portsc & XHCI_PORTSC_CCS) == 0u) {
            continue;
        }
        XHCI_ENUM_TRACE("xhci%u: root port%u %s conn=%u enabled=%u powered=%u speed=%u portsc=%x\n",
                        (uint32_t)controller_index,
                        port,
                        phase,
                        (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
                        (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
                        (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
                        (portsc >> 10) & 0x0fu,
                        portsc);
    }
}

int xhci_alloc_page(uint64_t *phys_out, void **virt_out) {
    uint64_t phys;
    void *virt;

    if (phys_out == 0 || virt_out == 0) {
        return 0;
    }
    phys = pmm_alloc_page_below(0x100000000ull);
    if (phys == 0u) {
        return 0;
    }
    virt = hal_phys_direct_map(phys);
    if (virt == 0) {
        (void)pmm_free_page(phys);
        return 0;
    }
    memset(virt, 0, XHCI_PAGE_SIZE);
    *phys_out = phys;
    *virt_out = virt;
    return 1;
}

static void xhci_free_page_record(uint64_t *phys, void **virt) {
    if (phys != 0 && *phys != 0u) {
        (void)pmm_free_page(*phys);
        *phys = 0u;
    }
    if (virt != 0) {
        *virt = 0;
    }
}

static void xhci_release_core_rings(uint32_t allocated_scratchpads) {
    if (g_xhci.scratchpad_array != 0) {
        for (uint32_t i = 0; i < allocated_scratchpads && i < XHCI_MAX_SCRATCHPADS; i++) {
            if (g_xhci.scratchpad_array[i] != 0u) {
                (void)pmm_free_page(g_xhci.scratchpad_array[i]);
                g_xhci.scratchpad_array[i] = 0u;
            }
        }
    }

    xhci_free_page_record(&g_xhci.scratchpad_array_phys, (void **)&g_xhci.scratchpad_array);
    xhci_free_page_record(&g_xhci.erst_phys, (void **)&g_xhci.erst);
    xhci_free_page_record(&g_xhci.event_ring_phys, (void **)&g_xhci.event_ring);
    xhci_free_page_record(&g_xhci.command_ring_phys, (void **)&g_xhci.command_ring);
    xhci_free_page_record(&g_xhci.dcbaa_phys, (void **)&g_xhci.dcbaa);
}

int xhci_alloc_core_rings(uint32_t scratchpads) {
    if (!xhci_alloc_page(&g_xhci.dcbaa_phys, (void **)&g_xhci.dcbaa) ||
        !xhci_alloc_page(&g_xhci.command_ring_phys, (void **)&g_xhci.command_ring) ||
        !xhci_alloc_page(&g_xhci.event_ring_phys, (void **)&g_xhci.event_ring) ||
        !xhci_alloc_page(&g_xhci.erst_phys, (void **)&g_xhci.erst)) {
        xhci_release_core_rings(0u);
        return 0;
    }
    g_xhci.command_ring[XHCI_RING_TRBS - 1u].control = (XHCI_TRB_LINK << 10) | 2u | 1u;
    g_xhci.command_ring[XHCI_RING_TRBS - 1u].parameter_lo = (uint32_t)g_xhci.command_ring_phys;
    g_xhci.command_ring[XHCI_RING_TRBS - 1u].parameter_hi = (uint32_t)(g_xhci.command_ring_phys >> 32);
    g_xhci.erst[0].ring_base_lo = (uint32_t)g_xhci.event_ring_phys;
    g_xhci.erst[0].ring_base_hi = (uint32_t)(g_xhci.event_ring_phys >> 32);
    g_xhci.erst[0].ring_size = XHCI_RING_TRBS;

    if (scratchpads != 0u) {
        if (scratchpads > XHCI_MAX_SCRATCHPADS) {
            kprint("xhci: unsupported scratchpads=%u max=%u\n", scratchpads, XHCI_MAX_SCRATCHPADS);
            xhci_release_core_rings(0u);
            return 0;
        }
        if (!xhci_alloc_page(&g_xhci.scratchpad_array_phys, (void **)&g_xhci.scratchpad_array)) {
            xhci_release_core_rings(0u);
            return 0;
        }
        for (uint32_t i = 0; i < scratchpads; i++) {
            uint64_t scratch_phys;
            void *scratch_virt;

            if (!xhci_alloc_page(&scratch_phys, &scratch_virt)) {
                xhci_release_core_rings(i);
                return 0;
            }
            (void)scratch_virt;
            g_xhci.scratchpad_array[i] = scratch_phys;
        }
        g_xhci.dcbaa[0] = g_xhci.scratchpad_array_phys;
    }
    return 1;
}

int xhci_alloc_enum_device(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_page(&dev->input_context_phys, (void **)&dev->input_context) ||
        !xhci_alloc_page(&dev->device_context_phys, (void **)&dev->device_context) ||
        !xhci_alloc_page(&dev->ep0_ring_phys, (void **)&dev->ep0_ring) ||
        !xhci_alloc_page(&dev->descriptor_phys, (void **)&dev->descriptor)) {
        xhci_release_enum_device_resources(dev);
        return 0;
    }
    xhci_ring_trb_set(&dev->ep0_ring[XHCI_RING_TRBS - 1u],
                      dev->ep0_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    dev->ep0_enqueue = 0u;
    dev->ep0_cycle = 1u;
    return 1;
}

void xhci_release_enum_device_resources(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    if (dev->input_context_phys != 0u) {
        (void)pmm_free_page(dev->input_context_phys);
    }
    if (dev->device_context_phys != 0u) {
        (void)pmm_free_page(dev->device_context_phys);
    }
    if (dev->ep0_ring_phys != 0u) {
        (void)pmm_free_page(dev->ep0_ring_phys);
    }
    if (dev->descriptor_phys != 0u) {
        (void)pmm_free_page(dev->descriptor_phys);
    }
    if (dev->bulk_in_ring_phys != 0u) {
        (void)pmm_free_page(dev->bulk_in_ring_phys);
    }
    if (dev->bulk_out_ring_phys != 0u) {
        (void)pmm_free_page(dev->bulk_out_ring_phys);
    }
    if (dev->data_phys != 0u) {
        (void)pmm_free_page(dev->data_phys);
    }
    if (dev->read_cache_phys != 0u) {
        (void)pmm_free_page(dev->read_cache_phys);
    }
    if (dev->cbw_phys != 0u) {
        (void)pmm_free_page(dev->cbw_phys);
    }
    if (dev->csw_phys != 0u) {
        (void)pmm_free_page(dev->csw_phys);
    }
    memset(dev, 0, sizeof(*dev));
}

void xhci_release_msc_resources(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    if (dev->bulk_in_ring_phys != 0u) {
        (void)pmm_free_page(dev->bulk_in_ring_phys);
    }
    if (dev->bulk_out_ring_phys != 0u) {
        (void)pmm_free_page(dev->bulk_out_ring_phys);
    }
    if (dev->data_phys != 0u) {
        (void)pmm_free_page(dev->data_phys);
    }
    if (dev->read_cache_phys != 0u) {
        (void)pmm_free_page(dev->read_cache_phys);
    }
    if (dev->cbw_phys != 0u) {
        (void)pmm_free_page(dev->cbw_phys);
    }
    if (dev->csw_phys != 0u) {
        (void)pmm_free_page(dev->csw_phys);
    }
    dev->bulk_in_ring_phys = 0u;
    dev->bulk_out_ring_phys = 0u;
    dev->data_phys = 0u;
    dev->read_cache_phys = 0u;
    dev->cbw_phys = 0u;
    dev->csw_phys = 0u;
    dev->bulk_in_ring = 0;
    dev->bulk_out_ring = 0;
    dev->data = 0;
    dev->read_cache = 0;
    dev->cbw = 0;
    dev->csw = 0;
}

int xhci_alloc_msc_resources(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_page(&dev->bulk_in_ring_phys, (void **)&dev->bulk_in_ring) ||
        !xhci_alloc_page(&dev->bulk_out_ring_phys, (void **)&dev->bulk_out_ring) ||
        !xhci_alloc_page(&dev->data_phys, (void **)&dev->data) ||
        !xhci_alloc_page(&dev->read_cache_phys, (void **)&dev->read_cache) ||
        !xhci_alloc_page(&dev->cbw_phys, (void **)&dev->cbw) ||
        !xhci_alloc_page(&dev->csw_phys, (void **)&dev->csw)) {
        xhci_release_msc_resources(dev);
        return 0;
    }
    xhci_ring_trb_set(&dev->bulk_in_ring[XHCI_RING_TRBS - 1u],
                      dev->bulk_in_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    xhci_ring_trb_set(&dev->bulk_out_ring[XHCI_RING_TRBS - 1u],
                      dev->bulk_out_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    dev->bulk_in_enqueue = 0u;
    dev->bulk_out_enqueue = 0u;
    dev->bulk_in_cycle = 1u;
    dev->bulk_out_cycle = 1u;
    dev->bulk_in_ring_epoch = 1u;
    dev->bulk_out_ring_epoch = 1u;
    return 1;
}

int xhci_alloc_hid_keyboard_resources(struct xhci_hid_keyboard *kbd) {
    if (kbd == 0) {
        return 0;
    }
    if (!xhci_alloc_page(&kbd->interrupt_in_ring_phys, (void **)&kbd->interrupt_in_ring) ||
        !xhci_alloc_page(&kbd->report_phys, (void **)&kbd->report)) {
        return 0;
    }
    xhci_ring_trb_set(&kbd->interrupt_in_ring[XHCI_RING_TRBS - 1u],
                      kbd->interrupt_in_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    kbd->interrupt_in_enqueue = 0u;
    kbd->interrupt_in_cycle = 1u;
    return 1;
}

struct xhci_enum_device *xhci_alloc_device_record(void) {
    for (uint32_t i = 0; i < XHCI_MAX_ENUM_DEVICES; i++) {
        if (!g_enum_devices[i].used && g_enum_devices[i].slot_id == 0u) {
            return &g_enum_devices[i];
        }
    }
    kprint("xhci: enum device table full max=%u\n", XHCI_MAX_ENUM_DEVICES);
    return 0;
}
