#include "drivers/usb/ehci_internal.h"

uint8_t ehci_read8(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

void ehci_use_device_controller(const struct ehci_msc_device *dev) {
    if (dev != 0 && dev->controller.op != 0) {
        g_ehci = dev->controller;
    }
}

uint32_t ehci_read_cap32(uint32_t offset) {
    return *(volatile uint32_t *)(g_ehci.cap + offset);
}

uint32_t ehci_read_op(uint32_t offset) {
    return g_ehci.op[offset / 4u];
}

void ehci_write_op(uint32_t offset, uint32_t value) {
    g_ehci.op[offset / 4u] = value;
}

void ehci_delay_spin(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

void ehci_delay_ms(uint32_t ms) {
    while (ms != 0u) {
        uint32_t chunk = ms > 50u ? 50u : ms;
        uint16_t reload = (uint16_t)(((uint64_t)EHCI_PIT_RATE_HZ * chunk) / 1000ull);
        uint8_t speaker = hal_io_in8(0x61);
        uint32_t spin = 0u;

        if (reload == 0u) {
            reload = 1u;
        }
        hal_io_out8(0x61, (uint8_t)(speaker & ~0x03u));
        hal_io_out8(0x43, 0xb0);
        hal_io_out8(0x42, (uint8_t)(reload & 0xffu));
        hal_io_out8(0x42, (uint8_t)((reload >> 8) & 0xffu));
        hal_io_out8(0x61, (uint8_t)((speaker & ~0x02u) | 0x01u));
        while ((hal_io_in8(0x61) & 0x20u) == 0u && spin < 100000000u) {
            spin++;
        }
        hal_io_out8(0x61, speaker);
        if (spin >= 100000000u) {
            ehci_delay_spin(chunk * 1000000u);
        }
        ms -= chunk;
    }
}

void ehci_dma_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

int ehci_wait_op_clear(uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((ehci_read_op(offset) & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

int ehci_wait_op_set(uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((ehci_read_op(offset) & mask) != 0u) {
            return 1;
        }
    }
    return 0;
}

int ehci_start_controller(void) {
    uint32_t cmd = EHCI_USBCMD_RS;

    if (g_ehci_periodic_qh_head != 0u) {
        cmd |= EHCI_USBCMD_PSE;
    }
    kprint("ehci: start controller cmd_before=%x cmd=%x periodic=%x\n",
           ehci_read_op(0x00u), cmd, g_ehci_periodic_qh_head);
    ehci_write_op(0x04u, EHCI_USBSTS_CLEAR);
    ehci_write_op(0x08u, 0u);
    ehci_write_op(0x10u, 0u);
    if (g_ehci_periodic_list_phys != 0u) {
        ehci_write_op(0x14u, (uint32_t)g_ehci_periodic_list_phys);
    }
    if (g_ehci_async_head_phys != 0u) {
        ehci_write_op(0x18u, (uint32_t)g_ehci_async_head_phys);
    }
    (void)ehci_read_op(0x18u);
    ehci_write_op(0x00u, cmd);
    (void)ehci_read_op(0x00u);
    if (!ehci_wait_op_clear(0x04u, EHCI_USBSTS_HCHALTED)) {
        return 0;
    }
    ehci_write_op(0x40u, 1u);
    (void)ehci_read_op(0x40u);
    ehci_delay_ms(5u);
    return ehci_wait_op_clear(0x04u, EHCI_USBSTS_HCHALTED);
}

int ehci_reset_controller_runtime(void) {
    kprint("ehci: runtime reset cmd=%x sts=%x\n",
           ehci_read_op(0x00u), ehci_read_op(0x04u));
    ehci_write_op(0x00u, ehci_read_op(0x00u) & ~(EHCI_USBCMD_RS | EHCI_USBCMD_PSE | EHCI_USBCMD_ASE));
    (void)ehci_wait_op_set(0x04u, EHCI_USBSTS_HCHALTED);
    ehci_write_op(0x00u, EHCI_USBCMD_HCRESET);
    if (!ehci_wait_op_clear(0x00u, EHCI_USBCMD_HCRESET)) {
        return 0;
    }
    return ehci_start_controller();
}

int ehci_ensure_running(void) {
    uint32_t cmd = ehci_read_op(0x00u);

    ehci_write_op(0x04u, EHCI_USBSTS_CLEAR);
    if ((cmd & EHCI_USBCMD_RS) == 0u) {
        ehci_write_op(0x00u, cmd | EHCI_USBCMD_RS);
    }
    if (ehci_wait_op_clear(0x04u, EHCI_USBSTS_HCHALTED)) {
        return 1;
    }
    return ehci_reset_controller_runtime();
}

uint32_t ehci_port_write_value(uint32_t port) {
    return port & ~EHCI_PORT_WRITE_CLEAR_BITS;
}

int ehci_reset_port(uint32_t port_index, uint32_t *port_out) {
    uint32_t port_off = 0x44u + port_index * 4u;
    uint32_t port = ehci_read_op(port_off);

    if (!ehci_ensure_running()) {
        if (port_out != 0) {
            *port_out = port;
        }
        return 0;
    }
    if ((port & EHCI_PORT_CONNECT) == 0u) {
        return 0;
    }
    ehci_write_op(port_off, ehci_port_write_value(port | EHCI_PORT_POWER));
    ehci_delay_ms(100u);
    port = ehci_read_op(port_off);
    ehci_write_op(port_off, ehci_port_write_value((port & ~EHCI_PORT_OWNER) |
                                                  EHCI_PORT_POWER |
                                                  EHCI_PORT_RESET));
    ehci_delay_ms(60u);
    ehci_write_op(port_off, ehci_port_write_value((ehci_read_op(port_off) & ~EHCI_PORT_RESET) |
                                                  EHCI_PORT_POWER));
    for (uint32_t i = 0; i < 200u; i++) {
        port = ehci_read_op(port_off);
        if ((port & EHCI_PORT_RESET) == 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    for (uint32_t i = 0; i < 200u; i++) {
        port = ehci_read_op(port_off);
        if ((port & EHCI_PORT_CONNECT) == 0u || (port & EHCI_PORT_ENABLE) != 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    port = ehci_read_op(port_off);
    if (port_out != 0) {
        *port_out = port;
    }
    return (port & EHCI_PORT_ENABLE) != 0u;
}

void ehci_write_name(char *dst, uint32_t index) {
    dst[0] = 'u';
    dst[1] = 's';
    dst[2] = 'b';
    dst[3] = 'm';
    dst[4] = 's';
    dst[5] = 'c';
    dst[6] = (char)('0' + index);
    dst[7] = '\0';
}

int ehci_alloc_async_head(void) {
    g_ehci_async_head_phys = pmm_alloc_page_below(0x100000000ull);
    g_ehci_async_dummy_qtd_phys = pmm_alloc_page_below(0x100000000ull);
    g_ehci_periodic_list_phys = pmm_alloc_page_below(0x100000000ull);
    if (g_ehci_async_head_phys == 0u || g_ehci_async_dummy_qtd_phys == 0u ||
        g_ehci_periodic_list_phys == 0u ||
        g_ehci_async_head_phys > 0xffffffffull || g_ehci_async_dummy_qtd_phys > 0xffffffffull ||
        g_ehci_periodic_list_phys > 0xffffffffull) {
        if (g_ehci_async_head_phys != 0u) {
            (void)pmm_free_page(g_ehci_async_head_phys);
        }
        if (g_ehci_async_dummy_qtd_phys != 0u) {
            (void)pmm_free_page(g_ehci_async_dummy_qtd_phys);
        }
        if (g_ehci_periodic_list_phys != 0u) {
            (void)pmm_free_page(g_ehci_periodic_list_phys);
        }
        g_ehci_async_head_phys = 0u;
        g_ehci_async_dummy_qtd_phys = 0u;
        g_ehci_periodic_list_phys = 0u;
        return 0;
    }
    g_ehci_async_head = (struct ehci_qh *)hal_phys_direct_map(g_ehci_async_head_phys);
    g_ehci_async_dummy_qtd = (struct ehci_qtd *)hal_phys_direct_map(g_ehci_async_dummy_qtd_phys);
    g_ehci_periodic_list = (uint32_t *)hal_phys_direct_map(g_ehci_periodic_list_phys);
    if (g_ehci_async_head == 0 || g_ehci_async_dummy_qtd == 0 || g_ehci_periodic_list == 0) {
        (void)pmm_free_page(g_ehci_async_head_phys);
        (void)pmm_free_page(g_ehci_async_dummy_qtd_phys);
        (void)pmm_free_page(g_ehci_periodic_list_phys);
        g_ehci_async_head_phys = 0u;
        g_ehci_async_dummy_qtd_phys = 0u;
        g_ehci_periodic_list_phys = 0u;
        g_ehci_async_head = 0;
        g_ehci_async_dummy_qtd = 0;
        g_ehci_periodic_list = 0;
        return 0;
    }
    memset(g_ehci_async_head, 0, EHCI_PAGE_SIZE);
    memset(g_ehci_async_dummy_qtd, 0, EHCI_PAGE_SIZE);
    for (uint32_t i = 0; i < EHCI_PAGE_SIZE / sizeof(uint32_t); i++) {
        g_ehci_periodic_list[i] = EHCI_LINK_TERMINATE;
    }
    g_ehci_async_dummy_qtd->next = EHCI_LINK_TERMINATE;
    g_ehci_async_dummy_qtd->alt_next = EHCI_LINK_TERMINATE;
    g_ehci_async_dummy_qtd->token = EHCI_QTD_HALTED;
    return 1;
}

static void ehci_free_msc_memory_local(struct ehci_msc_device *dev) {
    if (dev == 0) {
        return;
    }
    if (dev->qh_phys != 0u) {
        (void)pmm_free_page(dev->qh_phys);
    }
    if (dev->qtd_phys != 0u) {
        (void)pmm_free_page(dev->qtd_phys);
    }
    if (dev->setup_phys != 0u) {
        (void)pmm_free_page(dev->setup_phys);
    }
    if (dev->data_phys != 0u) {
        for (uint32_t i = 0; i < EHCI_MSC_TRANSFER_PAGES; i++) {
            (void)pmm_free_page(dev->data_phys + (uint64_t)i * EHCI_PAGE_SIZE);
        }
    }
    if (dev->read_cache_phys != 0u) {
        for (uint32_t i = 0; i < EHCI_MSC_TRANSFER_PAGES; i++) {
            (void)pmm_free_page(dev->read_cache_phys + (uint64_t)i * EHCI_PAGE_SIZE);
        }
    }
    if (dev->cbw_phys != 0u) {
        (void)pmm_free_page(dev->cbw_phys);
    }
    if (dev->csw_phys != 0u) {
        (void)pmm_free_page(dev->csw_phys);
    }
    memset(dev, 0, sizeof(*dev));
}

int ehci_alloc_msc_memory(struct ehci_msc_device *dev) {
    dev->qh_phys = pmm_alloc_page_below(0x100000000ull);
    dev->qtd_phys = pmm_alloc_page_below(0x100000000ull);
    dev->setup_phys = pmm_alloc_page_below(0x100000000ull);
    dev->data_phys = pmm_alloc_contiguous_below(EHCI_MSC_TRANSFER_PAGES, 0x100000000ull);
    dev->read_cache_phys = pmm_alloc_contiguous_below(EHCI_MSC_TRANSFER_PAGES, 0x100000000ull);
    dev->cbw_phys = pmm_alloc_page_below(0x100000000ull);
    dev->csw_phys = pmm_alloc_page_below(0x100000000ull);
    if (dev->qh_phys == 0u || dev->qtd_phys == 0u || dev->setup_phys == 0u ||
        dev->data_phys == 0u || dev->read_cache_phys == 0u ||
        dev->cbw_phys == 0u || dev->csw_phys == 0u ||
        dev->qh_phys > 0xffffffffull || dev->qtd_phys > 0xffffffffull ||
        dev->setup_phys > 0xffffffffull || dev->data_phys > 0xffffffffull ||
        dev->read_cache_phys > 0xffffffffull ||
        dev->cbw_phys > 0xffffffffull || dev->csw_phys > 0xffffffffull) {
        ehci_free_msc_memory_local(dev);
        return 0;
    }
    dev->qh = (struct ehci_qh *)hal_phys_direct_map(dev->qh_phys);
    dev->qtd = (struct ehci_qtd *)hal_phys_direct_map(dev->qtd_phys);
    dev->setup = (uint8_t *)hal_phys_direct_map(dev->setup_phys);
    dev->data = (uint8_t *)hal_phys_direct_map(dev->data_phys);
    dev->read_cache = (uint8_t *)hal_phys_direct_map(dev->read_cache_phys);
    dev->cbw = (uint8_t *)hal_phys_direct_map(dev->cbw_phys);
    dev->csw = (uint8_t *)hal_phys_direct_map(dev->csw_phys);
    if (dev->qh == 0 || dev->qtd == 0 || dev->setup == 0 || dev->data == 0 ||
        dev->read_cache == 0 || dev->cbw == 0 || dev->csw == 0) {
        ehci_free_msc_memory_local(dev);
        return 0;
    }
    memset(dev->qh, 0, EHCI_PAGE_SIZE);
    memset(dev->qtd, 0, EHCI_PAGE_SIZE);
    memset(dev->setup, 0, EHCI_PAGE_SIZE);
    memset(dev->data, 0, EHCI_MSC_TRANSFER_BYTES);
    memset(dev->read_cache, 0, EHCI_MSC_TRANSFER_BYTES);
    memset(dev->cbw, 0, EHCI_PAGE_SIZE);
    memset(dev->csw, 0, EHCI_PAGE_SIZE);
    dev->read_cache_valid = 0u;
    dev->read_cache_count = 0u;
    dev->read_ahead_sectors = EHCI_MSC_READAHEAD_INITIAL_SECTORS;
    dev->sync_cache_supported = 1u;
    return 1;
}

void ehci_qtd_prepare(struct ehci_qtd *qtd,
                             uint64_t phys,
                             uint32_t next_phys,
                             uint32_t pid,
                             uint32_t bytes,
                             uint8_t toggle,
                             uint8_t ioc) {
    qtd->next = next_phys != 0u ? next_phys : EHCI_LINK_TERMINATE;
    qtd->alt_next = EHCI_LINK_TERMINATE;
    qtd->token = EHCI_QTD_ACTIVE |
                 (3u << EHCI_QTD_CERR_SHIFT) |
                 (pid << 8) |
                 (bytes << 16) |
                 (ioc ? EHCI_QTD_IOC : 0u) |
                 (toggle ? EHCI_QTD_TOGGLE : 0u);
    qtd->buffer[0] = (uint32_t)phys;
    qtd->buffer[1] = (uint32_t)((phys + 0x1000u) & 0xfffff000u);
    qtd->buffer[2] = (uint32_t)((phys + 0x2000u) & 0xfffff000u);
    qtd->buffer[3] = (uint32_t)((phys + 0x3000u) & 0xfffff000u);
    qtd->buffer[4] = (uint32_t)((phys + 0x4000u) & 0xfffff000u);
    qtd->buffer_hi[0] = 0u;
    qtd->buffer_hi[1] = 0u;
    qtd->buffer_hi[2] = 0u;
    qtd->buffer_hi[3] = 0u;
    qtd->buffer_hi[4] = 0u;
}

uint32_t ehci_qh_speed_bits(uint8_t speed) {
    if (speed == EHCI_DEV_SPEED_LOW) {
        return EHCI_QH_EPS_LOW;
    }
    if (speed == EHCI_DEV_SPEED_HIGH) {
        return EHCI_QH_EPS_HIGH;
    }
    return EHCI_QH_EPS_FULL;
}

void ehci_qh_prepare(struct ehci_msc_device *dev,
                            uint8_t addr,
                            uint8_t ep,
                            uint16_t mps,
                            uint8_t control,
                            uint32_t first_qtd_phys,
                            uint8_t qtd_controls_toggle,
                            uint8_t initial_toggle) {
    uint8_t active_is_head = g_ehci_async_head == 0 ? 1u : 0u;
    uint8_t speed = dev->speed;

    memset(dev->qh, 0, sizeof(*dev->qh));
    if (speed > EHCI_DEV_SPEED_HIGH) {
        speed = EHCI_DEV_SPEED_HIGH;
    }
    dev->qh->horiz_link = (g_ehci_async_head != 0 ? (uint32_t)g_ehci_async_head_phys : (uint32_t)dev->qh_phys) |
                          EHCI_LINK_TYPE_QH;
    dev->qh->ep_char = ((uint32_t)addr & 0x7fu) |
                       ((uint32_t)ep << 8) |
                       ehci_qh_speed_bits(speed) |
                       (qtd_controls_toggle ? EHCI_QH_DTC : 0u) |
                       (active_is_head ? EHCI_QH_HEAD : 0u) |
                       (control && speed != EHCI_DEV_SPEED_HIGH ? EHCI_QH_CONTROL_EP : 0u) |
                       (control ? EHCI_QH_RL_HS : 0u) |
                       ((uint32_t)mps << 16);
    dev->qh->ep_cap = EHCI_QH_MULT_1 |
                      (speed != EHCI_DEV_SPEED_HIGH && dev->hub_addr != 0u
                       ? ((uint32_t)dev->hub_addr << 16) | ((uint32_t)dev->hub_port << 23)
                       : 0u);
    dev->qh->current_qtd = 0u;
    dev->qh->next_qtd = first_qtd_phys;
    dev->qh->alt_next_qtd = EHCI_LINK_TERMINATE;
    dev->qh->token = initial_toggle ? EHCI_QTD_TOGGLE : 0u;
}

void ehci_async_head_prepare(struct ehci_msc_device *dev) {
    if (g_ehci_async_head == 0 || dev == 0) {
        return;
    }
    memset(g_ehci_async_head, 0, sizeof(*g_ehci_async_head));
    g_ehci_async_head->horiz_link = (uint32_t)dev->qh_phys | EHCI_LINK_TYPE_QH;
    g_ehci_async_head->ep_char = EHCI_QH_HEAD;
    g_ehci_async_head->ep_cap = 0u;
    g_ehci_async_head->current_qtd = 0u;
    g_ehci_async_head->next_qtd = EHCI_LINK_TERMINATE;
    g_ehci_async_head->alt_next_qtd = g_ehci_async_dummy_qtd_phys != 0u
                                 ? (uint32_t)g_ehci_async_dummy_qtd_phys
                                 : EHCI_LINK_TERMINATE;
    g_ehci_async_head->token = EHCI_QTD_HALTED;
}

int ehci_run_async_transfer(struct ehci_msc_device *dev,
                                   uint32_t qtd_count,
                                   uint32_t spin_limit) {
    uint32_t async_head_phys;

    if (spin_limit == 0u) {
        spin_limit = EHCI_ASYNC_BULK_META_SPINS;
    }
    ehci_use_device_controller(dev);
    if (!ehci_ensure_running()) {
        return 0;
    }
    ehci_async_head_prepare(dev);
    async_head_phys = g_ehci_async_head != 0 ? (uint32_t)g_ehci_async_head_phys : (uint32_t)dev->qh_phys;
    ehci_write_op(0x04u, EHCI_USBSTS_CLEAR);
    ehci_dma_barrier();
    ehci_write_op(0x18u, async_head_phys);
    (void)ehci_read_op(0x18u);
    ehci_write_op(0x00u, ehci_read_op(0x00u) | EHCI_USBCMD_ASE);
    (void)ehci_read_op(0x00u);
    if (!ehci_wait_op_set(0x04u, EHCI_USBSTS_ASS)) {
        return 0;
    }
    for (uint32_t spin = 0; spin < spin_limit; spin++) {
        uint8_t qtd_active = 0u;
        uint8_t halted = 0u;

        for (uint32_t i = 0; i < qtd_count; i++) {
            if ((dev->qtd[i].token & EHCI_QTD_ACTIVE) != 0u) {
                qtd_active = 1u;
            }
            if ((dev->qtd[i].token & EHCI_QTD_HALTED) != 0u) {
                halted = 1u;
            }
        }
        if (!qtd_active) {
            ehci_write_op(0x00u, ehci_read_op(0x00u) & ~EHCI_USBCMD_ASE);
            (void)ehci_wait_op_clear(0x04u, EHCI_USBSTS_ASS);
            return !halted;
        }
        if ((spin & 0xfffu) == 0xfffu) {
            hal_cpu_wait_for_event();
        } else {
            hal_cpu_relax();
        }
    }
    ehci_write_op(0x00u, ehci_read_op(0x00u) & ~EHCI_USBCMD_ASE);
    (void)ehci_wait_op_clear(0x04u, EHCI_USBSTS_ASS);
    return 0;
}

void ehci_log_transfer_state(const char *label,
                                    uint32_t port_index,
                                    struct ehci_msc_device *dev,
                                    uint32_t qtd_count) {
    uint32_t port_off = 0x44u + port_index * 4u;

    if (dev == 0) {
        return;
    }
    kprint("ehci: %s port%u usbcmd=%x usbsts=%x portsc=%x qh_token=%x\n",
           label,
           port_index,
           ehci_read_op(0x00u),
           ehci_read_op(0x04u),
           ehci_read_op(port_off),
           dev->qh != 0 ? dev->qh->token : 0u);
    if (g_ehci_async_head != 0) {
        kprint("ehci: %s async=%x head_next=%x head_info=%x head_qtd=%x head_alt=%x head_token=%x\n",
               label,
               ehci_read_op(0x18u),
               g_ehci_async_head->horiz_link,
               g_ehci_async_head->ep_char,
               g_ehci_async_head->next_qtd,
               g_ehci_async_head->alt_next_qtd,
               g_ehci_async_head->token);
    }
    if (dev->qh != 0) {
        kprint("ehci: %s qh_link=%x qh_info=%x qh_cap=%x qh_current=%x qh_next=%x qh_alt=%x qh_hi0=%x\n",
               label,
               dev->qh->horiz_link,
               dev->qh->ep_char,
               dev->qh->ep_cap,
               dev->qh->current_qtd,
               dev->qh->next_qtd,
               dev->qh->alt_next_qtd,
               dev->qh->buffer_hi[0]);
    }
    for (uint32_t i = 0; i < qtd_count && i < 3u; i++) {
        kprint("ehci: %s qtd%u token=%x next=%x alt=%x buf0=%x hi0=%x\n",
               label,
               i,
               dev->qtd[i].token,
               dev->qtd[i].next,
               dev->qtd[i].alt_next,
               dev->qtd[i].buffer[0],
               dev->qtd[i].buffer_hi[0]);
    }
}

int ehci_control_transfer(struct ehci_msc_device *dev,
                                 uint8_t addr,
                                 uint16_t mps,
                                 const struct usb_ctrl_request *req,
                                 void *buffer,
                                 uint32_t length,
                                 uint8_t data_in) {
    uint32_t q0 = (uint32_t)dev->qtd_phys;
    uint32_t q1 = q0 + sizeof(struct ehci_qtd);
    uint32_t q2 = q1 + sizeof(struct ehci_qtd);

    if (dev == 0 || req == 0 || length > EHCI_PAGE_SIZE) {
        return 0;
    }
    ehci_use_device_controller(dev);
    memset(dev->qtd, 0, EHCI_PAGE_SIZE);
    memset(dev->setup, 0, 8u);
    dev->setup[0] = req->type;
    dev->setup[1] = req->request;
    usb_write_u16le(dev->setup + 2, req->value);
    usb_write_u16le(dev->setup + 4, req->index);
    usb_write_u16le(dev->setup + 6, req->length);
    if (buffer != 0 && length != 0u && !data_in) {
        memcpy(dev->data, buffer, length);
    }

    if (length != 0u) {
        ehci_qtd_prepare(&dev->qtd[0], dev->setup_phys, q1, EHCI_QTD_PID_SETUP, 8u, 0u, 0u);
        ehci_qtd_prepare(&dev->qtd[1], dev->data_phys, q2,
                         data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT,
                         length, 1u, 0u);
        ehci_qtd_prepare(&dev->qtd[2], dev->data_phys, 0u,
                         data_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN,
                         0u, 1u, 1u);
    } else {
        ehci_qtd_prepare(&dev->qtd[0], dev->setup_phys, q1, EHCI_QTD_PID_SETUP, 8u, 0u, 0u);
        ehci_qtd_prepare(&dev->qtd[1], dev->data_phys, 0u, EHCI_QTD_PID_IN, 0u, 1u, 1u);
    }
    ehci_qh_prepare(dev, addr, 0u, mps, 1u, q0, 1u, 0u);
    if (!ehci_run_async_transfer(dev,
                                 length != 0u ? 3u : 2u,
                                 EHCI_ASYNC_CONTROL_SPINS)) {
        return 0;
    }
    if (buffer != 0 && length != 0u && data_in) {
        memcpy(buffer, dev->data, length);
    }
    return 1;
}

int ehci_bulk_transfer(struct ehci_msc_device *dev,
                              uint8_t ep_addr,
                              uint16_t mps,
                              uint8_t *toggle,
                              uint64_t phys,
                              uint32_t bytes,
                              uint8_t in,
                              uint32_t *token_out,
                              uint32_t spin_limit) {
    uint32_t q0 = (uint32_t)dev->qtd_phys;
    uint32_t token;

    if (dev == 0 || toggle == 0 || bytes > EHCI_MSC_TRANSFER_BYTES || mps == 0u) {
        return 0;
    }
    if (token_out != 0) {
        *token_out = 0u;
    }
    ehci_use_device_controller(dev);
    memset(dev->qtd, 0, EHCI_PAGE_SIZE);
    ehci_qtd_prepare(&dev->qtd[0], phys, 0u, in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, bytes, 0u, 1u);
    ehci_qh_prepare(dev, dev->address, ep_addr & 0x0fu, mps, 0u, q0, 0u, *toggle);
    if (spin_limit == 0u) {
        spin_limit = bytes > 64u ? EHCI_ASYNC_BULK_DATA_SPINS : EHCI_ASYNC_BULK_META_SPINS;
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: BULK submit ep=%x num=%u dir=%u bytes=%u mps=%u toggle=%u phys=%lx spin=%u\n",
               (uint32_t)ep_addr,
               (uint32_t)(ep_addr & 0x0fu),
               (uint32_t)in,
               bytes,
               (uint32_t)mps,
               (uint32_t)*toggle,
               phys,
               spin_limit);
        kprint("ehci: QH ep_char=%x ep_cap=%x next_qtd=%x token=%x\n",
               dev->qh->ep_char,
               dev->qh->ep_cap,
               dev->qh->next_qtd,
               dev->qh->token);
        kprint("ehci: QTD0 next=%x token=%x buf0=%x\n",
               dev->qtd[0].next,
               dev->qtd[0].token,
               dev->qtd[0].buffer[0]);
        kprint("ehci: async_head phys=%x op_asynclistaddr=%x usbcmd=%x\n",
               (uint32_t)g_ehci_async_head_phys,
               ehci_read_op(0x18u),
               ehci_read_op(0x00u));
    }
    if (!ehci_run_async_transfer(dev,
                                 1u,
                                 spin_limit)) {
        if (token_out != 0) {
            *token_out = dev->qtd[0].token;
        }
        return 0;
    }
    token = dev->qtd[0].token;
    if (token_out != 0) {
        *token_out = token;
    }
    *toggle = (dev->qh->token & EHCI_QTD_TOGGLE) != 0u ? 1u : 0u;
    return 1;
}

static uint16_t ehci_interrupt_interval_power2_floor(uint16_t value) {
    uint16_t interval = 1u;

    if (value == 0u) {
        return 1u;
    }
    while (interval < 1024u && (uint16_t)(interval << 1u) <= value) {
        interval = (uint16_t)(interval << 1u);
    }
    return interval;
}

static uint16_t ehci_interrupt_interval_frames(uint8_t speed, uint8_t interval) {
    uint16_t frames;

    if (interval == 0u) {
        interval = 10u;
    }
    if (speed == EHCI_DEV_SPEED_HIGH) {
        uint8_t exponent = interval > 16u ? 16u : interval;
        uint32_t microframes = 1u << (exponent - 1u);

        frames = (uint16_t)((microframes + 7u) / 8u);
    } else {
        frames = interval;
    }
    if (frames == 0u) {
        frames = 1u;
    }
    if (frames > 1024u) {
        frames = 1024u;
    }
    return ehci_interrupt_interval_power2_floor(frames);
}

static struct ehci_msc_device *ehci_periodic_xfer_at(uint32_t index) {
    uint32_t keyboards = g_ehci_hid_keyboard_count;
    uint32_t mice = g_ehci_hid_mouse_count;

    if (keyboards > EHCI_MAX_HID_KEYBOARDS) {
        keyboards = EHCI_MAX_HID_KEYBOARDS;
    }
    if (mice > EHCI_MAX_HID_MICE) {
        mice = EHCI_MAX_HID_MICE;
    }
    if (index < keyboards) {
        return &g_ehci_hid_keyboards[index].xfer;
    }
    index -= keyboards;
    if (index < mice) {
        return &g_ehci_hid_mice[index].xfer;
    }
    return 0;
}

static uint32_t ehci_periodic_xfer_count(void) {
    uint32_t keyboards = g_ehci_hid_keyboard_count;
    uint32_t mice = g_ehci_hid_mouse_count;

    if (keyboards > EHCI_MAX_HID_KEYBOARDS) {
        keyboards = EHCI_MAX_HID_KEYBOARDS;
    }
    if (mice > EHCI_MAX_HID_MICE) {
        mice = EHCI_MAX_HID_MICE;
    }
    return keyboards + mice;
}

static uint32_t ehci_periodic_xfer_order(const struct ehci_msc_device *dev) {
    uint32_t count = ehci_periodic_xfer_count();

    for (uint32_t i = 0; i < count; i++) {
        if (ehci_periodic_xfer_at(i) == dev) {
            return i;
        }
    }
    return 0xffffffffu;
}

static int ehci_periodic_xfer_active(const struct ehci_msc_device *dev) {
    return dev != 0 && dev->controller.op == g_ehci.op &&
           dev->controller.schedule == g_ehci.schedule &&
           dev->qh != 0 && dev->qh_phys != 0u &&
           dev->interrupt_interval_frames != 0u;
}

static int ehci_periodic_xfer_due_at(const struct ehci_msc_device *dev, uint32_t frame) {
    uint16_t interval;
    uint16_t phase;

    if (!ehci_periodic_xfer_active(dev)) {
        return 0;
    }
    interval = dev->interrupt_interval_frames;
    phase = dev->interrupt_phase_frames;
    if (interval == 0u) {
        interval = 1u;
    }
    if (phase >= interval) {
        phase = 0u;
    }
    return (frame & (uint32_t)(interval - 1u)) == phase;
}

static uint32_t ehci_periodic_phase_load(const struct ehci_msc_device *skip, uint16_t phase) {
    uint32_t count = ehci_periodic_xfer_count();
    uint32_t load = 0u;

    for (uint32_t i = 0; i < count; i++) {
        struct ehci_msc_device *xfer = ehci_periodic_xfer_at(i);

        if (xfer != skip && ehci_periodic_xfer_due_at(xfer, phase)) {
            load++;
        }
    }
    return load;
}

static uint16_t ehci_interrupt_phase_frames(const struct ehci_msc_device *dev, uint16_t interval) {
    uint16_t best_phase = 0u;
    uint32_t best_load = 0xffffffffu;

    if (interval <= 1u) {
        return 0u;
    }
    for (uint16_t phase = 0u; phase < interval; phase++) {
        uint32_t load = ehci_periodic_phase_load(dev, phase);

        if (load < best_load) {
            best_load = load;
            best_phase = phase;
        }
    }
    return best_phase;
}

static int ehci_periodic_successor_due_after(const struct ehci_msc_device *first,
                                             const struct ehci_msc_device *next) {
    uint16_t first_interval;
    uint16_t next_interval;

    if (!ehci_periodic_xfer_active(first) || !ehci_periodic_xfer_active(next) || first == next) {
        return 0;
    }
    first_interval = first->interrupt_interval_frames;
    next_interval = next->interrupt_interval_frames;
    if (next_interval > first_interval) {
        return 0;
    }
    return (first->interrupt_phase_frames & (uint16_t)(next_interval - 1u)) ==
           next->interrupt_phase_frames;
}

static struct ehci_msc_device *ehci_periodic_best_successor(struct ehci_msc_device *dev) {
    uint32_t count = ehci_periodic_xfer_count();
    uint32_t dev_order = ehci_periodic_xfer_order(dev);
    struct ehci_msc_device *best = 0;
    uint32_t best_order = 0xffffffffu;

    for (uint32_t i = 0; i < count; i++) {
        struct ehci_msc_device *candidate = ehci_periodic_xfer_at(i);
        uint32_t candidate_order;

        if (!ehci_periodic_successor_due_after(dev, candidate)) {
            continue;
        }
        candidate_order = ehci_periodic_xfer_order(candidate);
        if (candidate->interrupt_interval_frames == dev->interrupt_interval_frames &&
            candidate_order <= dev_order) {
            continue;
        }
        if (best == 0 ||
            candidate->interrupt_interval_frames > best->interrupt_interval_frames ||
            (candidate->interrupt_interval_frames == best->interrupt_interval_frames &&
             candidate_order < best_order)) {
            best = candidate;
            best_order = candidate_order;
        }
    }
    return best;
}

static struct ehci_msc_device *ehci_periodic_best_head_for_frame(uint32_t frame) {
    uint32_t count = ehci_periodic_xfer_count();
    struct ehci_msc_device *best = 0;
    uint32_t best_order = 0xffffffffu;

    for (uint32_t i = 0; i < count; i++) {
        struct ehci_msc_device *candidate = ehci_periodic_xfer_at(i);

        if (!ehci_periodic_xfer_due_at(candidate, frame)) {
            continue;
        }
        if (best == 0 ||
            candidate->interrupt_interval_frames > best->interrupt_interval_frames ||
            (candidate->interrupt_interval_frames == best->interrupt_interval_frames &&
             i < best_order)) {
            best = candidate;
            best_order = i;
        }
    }
    return best;
}

static void ehci_periodic_rebuild_links(void) {
    uint32_t count = ehci_periodic_xfer_count();

    g_ehci_periodic_qh_head = 0u;
    for (uint32_t i = 0; i < count; i++) {
        struct ehci_msc_device *xfer = ehci_periodic_xfer_at(i);
        struct ehci_msc_device *next;

        if (!ehci_periodic_xfer_active(xfer)) {
            continue;
        }
        next = ehci_periodic_best_successor(xfer);
        xfer->qh->horiz_link = next != 0
                                   ? (uint32_t)next->qh_phys | EHCI_LINK_TYPE_QH
                                   : EHCI_LINK_TERMINATE;
        if (g_ehci_periodic_qh_head == 0u) {
            g_ehci_periodic_qh_head = (uint32_t)xfer->qh_phys | EHCI_LINK_TYPE_QH;
        }
    }
}

static uint8_t ehci_interrupt_split_start_mask(const struct ehci_msc_device *dev) {
    uint8_t microframe;

    if (dev == 0 || dev->speed == EHCI_DEV_SPEED_HIGH || dev->hub_addr == 0u) {
        return EHCI_QH_INTR_S_MASK_1;
    }
    microframe = (uint8_t)(dev->interrupt_phase_frames & 0x03u);
    return (uint8_t)(1u << microframe);
}

static uint8_t ehci_interrupt_split_complete_mask(const struct ehci_msc_device *dev, uint16_t mps) {
    uint8_t start_microframe;
    uint8_t complete_start;
    uint8_t complete_count;
    uint8_t mask = 0u;

    if (dev == 0 || dev->speed == EHCI_DEV_SPEED_HIGH || dev->hub_addr == 0u) {
        return 0u;
    }
    start_microframe = (uint8_t)(dev->interrupt_phase_frames & 0x03u);
    complete_start = (uint8_t)(start_microframe + 2u);
    complete_count = (dev->speed == EHCI_DEV_SPEED_LOW || mps > 32u) ? 4u : 3u;
    for (uint8_t i = 0u; i < complete_count && complete_start + i < 8u; i++) {
        mask |= (uint8_t)(1u << (complete_start + i));
    }
    return mask;
}

static void ehci_periodic_list_set_head(void) {
    if (g_ehci_periodic_list == 0) {
        return;
    }
    ehci_periodic_rebuild_links();
    for (uint32_t frame = 0; frame < EHCI_PAGE_SIZE / sizeof(uint32_t); frame++) {
        struct ehci_msc_device *head = ehci_periodic_best_head_for_frame(frame);

        g_ehci_periodic_list[frame] = head != 0
                                          ? (uint32_t)head->qh_phys | EHCI_LINK_TYPE_QH
                                          : EHCI_LINK_TERMINATE;
    }
}

static void ehci_qh_prepare_interrupt(struct ehci_msc_device *dev,
                                             uint8_t ep,
                                             uint16_t mps,
                                             uint32_t first_qtd_phys,
                                             uint8_t initial_toggle) {
    uint8_t start_mask = ehci_interrupt_split_start_mask(dev);
    uint8_t complete_mask = ehci_interrupt_split_complete_mask(dev, mps);

    ehci_qh_prepare(dev, dev->address, ep, mps, 0u, first_qtd_phys, 0u, initial_toggle);
    dev->qh->horiz_link = EHCI_LINK_TERMINATE;
    dev->qh->ep_cap = EHCI_QH_MULT_1 | start_mask | ((uint32_t)complete_mask << EHCI_QH_INTR_C_MASK_SHIFT);
    if (dev->speed != EHCI_DEV_SPEED_HIGH && dev->hub_addr != 0u) {
        dev->qh->ep_cap |= ((uint32_t)dev->hub_addr << 16) |
                           ((uint32_t)dev->hub_port << 23);
    }
}

static void ehci_interrupt_qtd_arm(struct ehci_msc_device *dev,
                                          uint8_t *toggle,
                                          uint64_t phys,
                                          uint32_t bytes,
                                          uint8_t in) {
    memset(dev->qtd, 0, EHCI_PAGE_SIZE);
    ehci_qtd_prepare(&dev->qtd[0], phys, 0u, in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, bytes, 0u, 1u);
    dev->qh->current_qtd = 0u;
    dev->qh->next_qtd = (uint32_t)dev->qtd_phys;
    dev->qh->alt_next_qtd = EHCI_LINK_TERMINATE;
    dev->qh->token = *toggle != 0u ? EHCI_QTD_TOGGLE : 0u;
    dev->qh->buffer[0] = 0u;
    dev->qh->buffer[1] = 0u;
    dev->qh->buffer[2] = 0u;
    dev->qh->buffer[3] = 0u;
    dev->qh->buffer[4] = 0u;
    dev->qh->buffer_hi[0] = 0u;
    dev->qh->buffer_hi[1] = 0u;
    dev->qh->buffer_hi[2] = 0u;
    dev->qh->buffer_hi[3] = 0u;
    dev->qh->buffer_hi[4] = 0u;
}

static int ehci_periodic_enable_for(const struct ehci_msc_device *dev) {
    ehci_use_device_controller(dev);
    if (!ehci_ensure_running()) {
        return 0;
    }
    ehci_write_op(0x14u, (uint32_t)g_ehci_periodic_list_phys);
    (void)ehci_read_op(0x14u);
    ehci_write_op(0x00u, ehci_read_op(0x00u) | EHCI_USBCMD_PSE);
    (void)ehci_read_op(0x00u);
    return ehci_wait_op_set(0x04u, EHCI_USBSTS_PSS);
}

int ehci_interrupt_open(struct ehci_msc_device *dev,
                               uint8_t ep_addr,
                               uint16_t mps,
                               uint8_t interval,
                               uint8_t *toggle,
                               uint64_t phys,
                               uint32_t bytes,
                               uint8_t in) {
    if (dev == 0 || dev->qh == 0 || dev->qtd == 0 || toggle == 0 ||
        bytes > EHCI_MSC_TRANSFER_BYTES || mps == 0u ||
        dev->controller.op == 0 || dev->controller.schedule == 0) {
        return 0;
    }
    ehci_use_device_controller(dev);
    if (g_ehci_periodic_list == 0 || g_ehci_periodic_list_phys == 0u ||
        !ehci_ensure_running()) {
        return 0;
    }
    dev->interrupt_interval_frames = ehci_interrupt_interval_frames(dev->speed, interval);
    dev->interrupt_phase_frames = ehci_interrupt_phase_frames(dev, dev->interrupt_interval_frames);
    ehci_qh_prepare_interrupt(dev, ep_addr & 0x0fu, mps, (uint32_t)dev->qtd_phys, *toggle);
    ehci_interrupt_qtd_arm(dev, toggle, phys, bytes, in);
    ehci_periodic_list_set_head();
    ehci_dma_barrier();
    if (!ehci_periodic_enable_for(dev)) {
        dev->interrupt_interval_frames = 0u;
        dev->interrupt_phase_frames = 0u;
        dev->qh->horiz_link = EHCI_LINK_TERMINATE;
        ehci_periodic_list_set_head();
        return 0;
    }
    return 1;
}

int ehci_interrupt_poll(struct ehci_msc_device *dev, uint8_t *toggle, uint32_t *token_out) {
    uint32_t token;

    if (dev == 0 || dev->qtd == 0 || dev->qh == 0 || toggle == 0) {
        return 0;
    }
    token = dev->qtd[0].token;
    if (token_out != 0) {
        *token_out = token;
    }
    if ((token & EHCI_QTD_ACTIVE) != 0u || (token & EHCI_QTD_HALTED) != 0u) {
        return 0;
    }
    *toggle = (dev->qh->token & EHCI_QTD_TOGGLE) != 0u ? 1u : 0u;
    return 1;
}

void ehci_interrupt_rearm(struct ehci_msc_device *dev,
                                 uint8_t *toggle,
                                 uint64_t phys,
                                 uint32_t bytes,
                                 uint8_t in) {
    if (dev == 0 || dev->qh == 0 || dev->qtd == 0 || toggle == 0) {
        return;
    }
    ehci_interrupt_qtd_arm(dev, toggle, phys, bytes, in);
    ehci_dma_barrier();
}

void ehci_interrupt_close(struct ehci_msc_device *dev) {
    if (dev == 0 || dev->qh == 0 || dev->qh_phys == 0u) {
        return;
    }
    ehci_use_device_controller(dev);
    dev->interrupt_interval_frames = 0u;
    dev->interrupt_phase_frames = 0u;
    dev->qh->horiz_link = EHCI_LINK_TERMINATE;
    dev->qh->next_qtd = EHCI_LINK_TERMINATE;
    dev->qh->alt_next_qtd = EHCI_LINK_TERMINATE;
    dev->qh->token = EHCI_QTD_HALTED;
    if (dev->qtd != 0) {
        dev->qtd[0].next = EHCI_LINK_TERMINATE;
        dev->qtd[0].alt_next = EHCI_LINK_TERMINATE;
        dev->qtd[0].token = EHCI_QTD_HALTED;
    }
    ehci_periodic_list_set_head();
    ehci_dma_barrier();
}

int ehci_get_descriptor(struct ehci_msc_device *dev,
                               uint8_t addr,
                               uint16_t mps,
                               uint8_t type,
                               uint8_t index,
                               void *buffer,
                               uint16_t length) {
    struct usb_ctrl_request req;

    req.type = 0x80u;
    req.request = USB_REQ_GET_DESCRIPTOR;
    req.value = (uint16_t)(((uint16_t)type << 8) | index);
    req.index = 0u;
    req.length = length;
    return ehci_control_transfer(dev, addr, mps, &req, buffer, length, 1u);
}

int ehci_get_initial_device_descriptor(struct ehci_msc_device *dev,
                                              uint32_t port_index,
                                              uint8_t *buffer,
                                              uint16_t length,
                                              uint16_t mps,
                                              uint32_t *port_out) {
    uint8_t reset_failed = 0u;

    for (uint32_t attempt = 0; attempt < 3u; attempt++) {
        if (attempt != 0u && !ehci_reset_port(port_index, port_out)) {
            reset_failed = 1u;
            break;
        }
        ehci_delay_ms(20u);
        memset(buffer, 0, length);
        if (ehci_get_descriptor(dev, 0u, mps, USB_DESC_DEVICE, 0u, buffer, length)) {
            return 1;
        }
    }
    ehci_log_transfer_state(reset_failed ? "initial-desc-reset" : "initial-desc",
                            port_index,
                            dev,
                            3u);
    return 0;
}

int ehci_set_address(struct ehci_msc_device *dev, uint8_t address, uint16_t mps) {
    struct usb_ctrl_request req;

    req.type = 0x00u;
    req.request = USB_REQ_SET_ADDRESS;
    req.value = address;
    req.index = 0u;
    req.length = 0u;
    return ehci_control_transfer(dev, 0u, mps, &req, 0, 0u, 0u);
}

int ehci_set_configuration(struct ehci_msc_device *dev, uint8_t configuration, uint16_t mps) {
    struct usb_ctrl_request req;

    req.type = 0x00u;
    req.request = USB_REQ_SET_CONFIGURATION;
    req.value = configuration;
    req.index = 0u;
    req.length = 0u;
    return ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
}

void ehci_log_config_descriptor(const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0u;

    if (cfg == 0 || length < 9u) {
        return;
    }
    kprint("ehci: cfg total=%u value=%u ifaces=%u attrs=%x maxpwr=%u\n",
           (uint32_t)usb_read_u16le(cfg + 2),
           (uint32_t)cfg[5],
           (uint32_t)cfg[4],
           (uint32_t)cfg[7],
           (uint32_t)cfg[8]);
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            kprint("ehci: cfg bad off=%u len=%u type=%u total=%u\n",
                   offset,
                   (uint32_t)len,
                   (uint32_t)type,
                   length);
            break;
        }
        if (type == 4u && len >= 9u) {
            kprint("ehci: cfg if off=%u num=%u alt=%u eps=%u cls=%x sub=%x proto=%x\n",
                   offset,
                   (uint32_t)cfg[offset + 2u],
                   (uint32_t)cfg[offset + 3u],
                   (uint32_t)cfg[offset + 4u],
                   (uint32_t)cfg[offset + 5u],
                   (uint32_t)cfg[offset + 6u],
                   (uint32_t)cfg[offset + 7u]);
        } else if (type == 5u && len >= 7u) {
            kprint("ehci: cfg ep off=%u ep=%x attr=%x mps=%u interval=%u\n",
                   offset,
                   (uint32_t)cfg[offset + 2u],
                   (uint32_t)cfg[offset + 3u],
                   (uint32_t)usb_read_u16le(cfg + offset + 4u),
                   (uint32_t)cfg[offset + 6u]);
        } else {
            kprint("ehci: cfg desc off=%u len=%u type=%u\n",
                   offset,
                   (uint32_t)len,
                   (uint32_t)type);
        }
        offset += len;
    }
}
