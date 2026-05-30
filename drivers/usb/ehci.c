#include "drivers/usb/ehci_internal.h"

static int ehci_driver_init_local(void) {
    ehci_init();
    return 1;
}

const struct kernel_driver ehci_kernel_driver = {
    .name = "EHCI",
    .kind = KERNEL_DRIVER_KIND_USB,
    .init = ehci_driver_init_local,
    .exit = NULL,
};

struct ehci_regs g_ehci;
struct ehci_msc_device g_ehci_msc[EHCI_MAX_MSC];
uint32_t g_ehci_msc_count;
struct ehci_msc_device g_ehci_hubs[EHCI_MAX_HUBS];
uint32_t g_ehci_hub_count;
struct ehci_hid_keyboard g_ehci_hid_keyboards[EHCI_MAX_HID_KEYBOARDS];
uint32_t g_ehci_hid_keyboard_count;
struct ehci_hid_mouse g_ehci_hid_mice[EHCI_MAX_HID_MICE];
uint32_t g_ehci_hid_mouse_count;
uint8_t g_ehci_next_address;
struct keyboard_event g_ehci_hid_event_queue[EHCI_HID_EVENT_QUEUE_SIZE];
uint32_t g_ehci_hid_event_head;
uint32_t g_ehci_hid_event_tail;
uint32_t g_ehci_hid_event_count;
uint32_t g_ehci_hid_poll_divider;
uint32_t g_ehci_hid_last_repeat_tick;
uint32_t g_ehci_last_hotplug_tick;
uint64_t g_ehci_async_head_phys;
uint64_t g_ehci_async_dummy_qtd_phys;
uint64_t g_ehci_periodic_list_phys;
struct ehci_qh *g_ehci_async_head;
struct ehci_qtd *g_ehci_async_dummy_qtd;
uint32_t *g_ehci_periodic_list;

static void ehci_bios_handoff(struct pci_ehci_controller *ehci) {
    uint32_t hccparams;
    uint32_t eecp;
    uint32_t cap;
    uint32_t tried_handoff = 0u;
    uint32_t guard = 64u;

    hccparams = ehci_read_cap32(0x08u);
    eecp = (hccparams >> 8) & 0xffu;
    while (eecp != 0u && guard-- != 0u) {
        cap = pci_config_read32(ehci->bus, ehci->slot, ehci->function, (uint8_t)eecp);
        if ((cap & 0xffu) == 1u) {
            if ((cap & (1u << 16)) != 0u) {
                tried_handoff = 1u;
                pci_config_write8(ehci->bus, ehci->slot, ehci->function, (uint8_t)(eecp + 3u), 1u);
                for (uint32_t i = 0; i < 100u; i++) {
                    ehci_delay_ms(10u);
                    cap = pci_config_read32(ehci->bus, ehci->slot, ehci->function, (uint8_t)eecp);
                    if ((cap & (1u << 16)) == 0u) {
                        break;
                    }
                }
                if ((cap & (1u << 16)) != 0u) {
                    pci_config_write8(ehci->bus, ehci->slot, ehci->function, (uint8_t)(eecp + 2u), 0u);
                }
            }
            pci_config_write32(ehci->bus, ehci->slot, ehci->function, (uint8_t)(eecp + 4u), 0u);
            if (tried_handoff) {
                ehci_write_op(0x40u, 0u);
            }
        } else if ((cap & 0xffu) == 0u) {
            break;
        }
        eecp = (cap >> 8) & 0xffu;
    }
}

void ehci_init(void) {
    struct pci_ehci_controller ehci;

    g_ehci_msc_count = 0u;
    g_ehci_hub_count = 0u;
    g_ehci_hid_keyboard_count = 0u;
    g_ehci_hid_mouse_count = 0u;
    g_ehci_next_address = 1u;
    g_ehci_hid_event_head = 0u;
    g_ehci_hid_event_tail = 0u;
    g_ehci_hid_event_count = 0u;
    g_ehci_hid_poll_divider = 0u;
    g_ehci_hid_last_repeat_tick = 0u;
    g_ehci_last_hotplug_tick = 0u;
    g_ehci_async_head_phys = 0u;
    g_ehci_async_dummy_qtd_phys = 0u;
    g_ehci_periodic_list_phys = 0u;
    g_ehci_async_head = 0;
    g_ehci_async_dummy_qtd = 0;
    g_ehci_periodic_list = 0;
    memset(g_ehci_msc, 0, sizeof(g_ehci_msc));
    memset(g_ehci_hubs, 0, sizeof(g_ehci_hubs));
    memset(g_ehci_hid_keyboards, 0, sizeof(g_ehci_hid_keyboards));
    memset(g_ehci_hid_mice, 0, sizeof(g_ehci_hid_mice));
    if (!ehci_alloc_async_head()) {
        kprint("ehci: async head allocation failed\n");
        return;
    }

    for (uint32_t index = 0u; pci_find_ehci_controller_at(index, &ehci); index++) {
        uint32_t mmio;
        uint32_t hcsparams;

        pci_config_write16(ehci.bus, ehci.slot, ehci.function, 0x04,
                           (uint16_t)(pci_config_read16(ehci.bus, ehci.slot, ehci.function, 0x04) | 0x0006u));
        mmio = ehci.mmio_base & 0xfffffff0u;
        if (mmio == 0u) {
            continue;
        }
        g_ehci.cap = (volatile uint8_t *)hal_mmio_map(mmio, 0x1000u);
        if (g_ehci.cap == 0) {
            continue;
        }
        g_ehci.cap_length = ehci_read8(g_ehci.cap, 0x00u);
        g_ehci.op = (volatile uint32_t *)(g_ehci.cap + g_ehci.cap_length);
        hcsparams = ehci_read_cap32(0x04u);
        g_ehci.port_count = (uint8_t)(hcsparams & 0x0fu);
        if (g_ehci.port_count > EHCI_MAX_PORTS) {
            g_ehci.port_count = EHCI_MAX_PORTS;
        }
        ehci_bios_handoff(&ehci);
        ehci_write_op(0x00u, ehci_read_op(0x00u) & ~EHCI_USBCMD_RS);
        (void)ehci_wait_op_set(0x04u, EHCI_USBSTS_HCHALTED);
        ehci_write_op(0x00u, EHCI_USBCMD_HCRESET);
        if (!ehci_wait_op_clear(0x00u, EHCI_USBCMD_HCRESET)) {
            kprint("ehci: reset timeout bdf=%u:%u.%u\n",
                   (uint32_t)ehci.bus, (uint32_t)ehci.slot, (uint32_t)ehci.function);
            continue;
        }
        if (!ehci_start_controller()) {
            kprint("ehci: start timeout bdf=%u:%u.%u\n",
                   (uint32_t)ehci.bus, (uint32_t)ehci.slot, (uint32_t)ehci.function);
            continue;
        }
        kprint("ehci: controller%u bdf=%u:%u.%u mmio=%x ports=%u rev=multi-ehci-v11\n",
               index,
               (uint32_t)ehci.bus, (uint32_t)ehci.slot, (uint32_t)ehci.function,
               mmio, (uint32_t)g_ehci.port_count);

        for (uint32_t port = 0; port < g_ehci.port_count; port++) {
            (void)ehci_enumerate_port(port);
        }
    }
}

uint32_t ehci_msc_device_count(void) {
    return g_ehci_msc_count;
}

uint32_t ehci_hid_keyboard_count(void) {
    return g_ehci_hid_keyboard_count;
}

int ehci_poll_keyboard_event(struct keyboard_event *out) {
    uint8_t report[8];

    if (out == 0) {
        return 0;
    }
    if (ehci_hid_pop_event(out)) {
        return 1;
    }
    if (g_ehci_hid_keyboard_count == 0u) {
        return 0;
    }
    ehci_hid_tick_repeats_once();
    if (ehci_hid_pop_event(out)) {
        return 1;
    }
    g_ehci_hid_poll_divider++;
    if ((g_ehci_hid_poll_divider & 3u) != 0u) {
        return 0;
    }
    for (uint32_t i = 0; i < g_ehci_hid_keyboard_count; i++) {
        struct ehci_hid_keyboard *kbd = &g_ehci_hid_keyboards[i];

        if (!kbd->present) {
            continue;
        }
        memset(report, 0, sizeof(report));
        if (!ehci_hid_get_report(kbd, report)) {
            if (!kbd->report_fail_logged) {
                kprint("ehci: hidkbd%u report poll failed\n", i);
                kbd->report_fail_logged = 1u;
            }
            continue;
        }
        kbd->report_fail_logged = 0u;
        ehci_hid_process_report(kbd, report);
        if (ehci_hid_pop_event(out)) {
            return 1;
        }
    }
    return 0;
}

void ehci_poll_mouse_events(uint32_t tick) {
    uint8_t report[4];

    for (uint32_t i = 0; i < g_ehci_hid_mouse_count; i++) {
        struct ehci_hid_mouse *mouse = &g_ehci_hid_mice[i];

        if (!mouse->present) {
            continue;
        }
        memset(report, 0, sizeof(report));
        if (!ehci_hid_mouse_poll_interrupt_report(mouse, report) &&
            !ehci_hid_mouse_get_report(mouse, report)) {
            if (!mouse->report_fail_logged) {
                kprint("ehci: hidmouse%u report poll failed\n", i);
                mouse->report_fail_logged = 1u;
            }
            continue;
        }
        mouse->report_fail_logged = 0u;
        ehci_hid_mouse_process_report(mouse, report, tick);
    }
}
