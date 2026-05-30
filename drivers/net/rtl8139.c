#include "drivers/net/rtl8139.h"

#include "arch/x86/io.h"
#include "drivers/bus/pci.h"
#include "drivers/net/net_event.h"
#include "hal/hal.h"
#include "kernel/public/mem/pmm.h"

enum {
    RTL8139_VENDOR_ID = 0x10ecu,
    RTL8139_DEVICE_ID = 0x8139u,
    PCI_COMMAND_IO = 1u << 0,
    PCI_COMMAND_BUS_MASTER = 1u << 2,
    RTL8139_REG_MAC0 = 0x00,
    RTL8139_REG_TSD0 = 0x10,
    RTL8139_REG_TSAD0 = 0x20,
    RTL8139_REG_RBSTART = 0x30,
    RTL8139_REG_CHIPCMD = 0x37,
    RTL8139_REG_CAPR = 0x38,
    RTL8139_REG_CBR = 0x3a,
    RTL8139_REG_IMR = 0x3c,
    RTL8139_REG_ISR = 0x3e,
    RTL8139_REG_TCR = 0x40,
    RTL8139_REG_RCR = 0x44,
    RTL8139_REG_CONFIG1 = 0x52,
    RTL8139_REG_MSR = 0x58,
    RTL8139_CHIPCMD_TE = 0x04u,
    RTL8139_CHIPCMD_RE = 0x08u,
    RTL8139_CHIPCMD_RST = 0x10u,
    RTL8139_MSR_LINKB = 0x04u,
    RTL8139_MSR_SPEED_10 = 0x08u,
    RTL8139_ISR_ROK = 0x0001u,
    RTL8139_ISR_RER = 0x0002u,
    RTL8139_ISR_TOK = 0x0004u,
    RTL8139_ISR_TER = 0x0008u,
    RTL8139_ISR_RXOVW = 0x0010u,
    RTL8139_ISR_PUN = 0x0020u,
    RTL8139_ISR_FOVW = 0x0040u,
    RTL8139_TSD_SIZE_MASK = 0x1fffu,
    RTL8139_TSD_TOK = 1u << 15,
    RTL8139_TSD_TUN = 1u << 14,
    RTL8139_TSD_TABT = 1u << 30,
    RTL8139_TX_DESC_COUNT = 4u,
    RTL8139_RCR_AAP = 1u << 0,
    RTL8139_RCR_APM = 1u << 1,
    RTL8139_RCR_AM = 1u << 2,
    RTL8139_RCR_AB = 1u << 3,
    RTL8139_RCR_WRAP = 1u << 7,
    RTL8139_TX_BUFFER_BYTES = 2048u,
    RTL8139_RX_RING_BYTES = 8192u,
    RTL8139_RX_RING_PAD_BYTES = 16u,
    RTL8139_RX_RING_WRAP_BYTES = 1500u,
    RTL8139_RX_BUFFER_BYTES =
        RTL8139_RX_RING_BYTES + RTL8139_RX_RING_PAD_BYTES + RTL8139_RX_RING_WRAP_BYTES,
    RTL8139_RX_RING_MASK = RTL8139_RX_RING_BYTES - 1u,
    RTL8139_RX_BUFFER_EMPTY = 0x01u,
    RTL8139_IRQ_MASK_DEFAULT =
        RTL8139_ISR_ROK | RTL8139_ISR_RER | RTL8139_ISR_TOK | RTL8139_ISR_TER |
        RTL8139_ISR_RXOVW | RTL8139_ISR_PUN | RTL8139_ISR_FOVW,
    RTL8139_RX_STATUS_ROK = 0x0001u
};

static struct rtl8139_status g_rtl8139_status;
static uint64_t g_rtl8139_tx_phys[RTL8139_TX_DESC_COUNT];
static uint8_t *g_rtl8139_tx_buffer[RTL8139_TX_DESC_COUNT];
static uint32_t g_rtl8139_tx_slot;
static uint64_t g_rtl8139_rx_phys;
static uint8_t *g_rtl8139_rx_buffer;
static uint32_t g_rtl8139_rx_offset;

const struct kernel_driver rtl8139_kernel_driver = {
    .name = "RTL8139",
    .kind = KERNEL_DRIVER_KIND_NET,
    .init = rtl8139_init,
    .exit = NULL,
};

static void rtl8139_fill_test_frame(uint8_t *frame, uint32_t size) {
    static const char payload[] = "NexOS RTL8139 test frame";
    uint32_t i;

    for (i = 0; i < size; i++) {
        frame[i] = 0;
    }
    for (i = 0; i < 6u; i++) {
        frame[i] = 0xffu;
    }
    for (i = 0; i < 6u; i++) {
        frame[6u + i] = g_rtl8139_status.mac[i];
    }
    frame[12] = 0x88u;
    frame[13] = 0xb5u;
    for (i = 0; i < sizeof(payload) - 1u && 14u + i < size; i++) {
        frame[14u + i] = (uint8_t)payload[i];
    }
}

static void rtl8139_copy_frame(uint8_t *dst, const uint8_t *src, uint32_t bytes) {
    uint32_t i;

    for (i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
    for (; i < 60u; i++) {
        dst[i] = 0u;
    }
}

static uint16_t rtl8139_io_base_from_bar(uint32_t bar) {
    if ((bar & 0x1u) == 0u) {
        return 0;
    }
    return (uint16_t)(bar & 0xfffcu);
}

static void rtl8139_emit_status_event_if_changed(void) {
    net_event_emit_status(g_rtl8139_status.present,
                          g_rtl8139_status.initialized,
                          g_rtl8139_status.link_up,
                          g_rtl8139_status.speed_mbps);
}

static void rtl8139_refresh_runtime_status(void) {
    uint16_t io_base = g_rtl8139_status.io_base;
    uint8_t media_status;
    uint8_t i;

    if (!g_rtl8139_status.present || io_base == 0u) {
        return;
    }

    for (i = 0; i < 6u; i++) {
        g_rtl8139_status.mac[i] = inb((uint16_t)(io_base + RTL8139_REG_MAC0 + i));
    }
    g_rtl8139_status.chip_cmd = inb((uint16_t)(io_base + RTL8139_REG_CHIPCMD));
    g_rtl8139_status.intr_mask = inw((uint16_t)(io_base + RTL8139_REG_IMR));
    g_rtl8139_status.intr_status = inw((uint16_t)(io_base + RTL8139_REG_ISR));
    g_rtl8139_status.tx_config = inl((uint16_t)(io_base + RTL8139_REG_TCR));
    g_rtl8139_status.rx_config = inl((uint16_t)(io_base + RTL8139_REG_RCR));
    g_rtl8139_status.capr = inw((uint16_t)(io_base + RTL8139_REG_CAPR));
    g_rtl8139_status.cbr = inw((uint16_t)(io_base + RTL8139_REG_CBR));
    g_rtl8139_status.rx_read_offset = g_rtl8139_rx_offset;
    media_status = inb((uint16_t)(io_base + RTL8139_REG_MSR));
    g_rtl8139_status.media_status = media_status;
    g_rtl8139_status.link_up = (uint8_t)((media_status & RTL8139_MSR_LINKB) == 0u);
    g_rtl8139_status.speed_mbps = (uint16_t)(((media_status & RTL8139_MSR_SPEED_10) != 0u) ? 10u : 100u);
    rtl8139_emit_status_event_if_changed();
}

static int rtl8139_wait_reset_clear(uint16_t io_base) {
    uint32_t spins = 0;

    while (spins < 1000000u) {
        if ((inb((uint16_t)(io_base + RTL8139_REG_CHIPCMD)) & RTL8139_CHIPCMD_RST) == 0u) {
            return 1;
        }
        spins++;
    }
    return 0;
}

static int rtl8139_prepare_tx_buffer(void) {
    uint32_t slot;

    for (slot = 0; slot < RTL8139_TX_DESC_COUNT; slot++) {
        if (g_rtl8139_tx_buffer[slot] != 0) {
            continue;
        }
        if (g_rtl8139_tx_phys[slot] == 0) {
            g_rtl8139_tx_phys[slot] = pmm_alloc_page();
        }
        if (g_rtl8139_tx_phys[slot] == 0) {
            return 0;
        }
        g_rtl8139_tx_buffer[slot] = (uint8_t *)hal_phys_direct_map(g_rtl8139_tx_phys[slot]);
        if (g_rtl8139_tx_buffer[slot] == 0) {
            return 0;
        }
    }
    return 1;
}

static int rtl8139_prepare_rx_buffer(void) {
    if (g_rtl8139_rx_buffer != 0) {
        return 1;
    }
    if (g_rtl8139_rx_phys == 0) {
        g_rtl8139_rx_phys = pmm_alloc_contiguous(3u);
    }
    if (g_rtl8139_rx_phys == 0) {
        return 0;
    }
    g_rtl8139_rx_buffer = (uint8_t *)hal_phys_direct_map(g_rtl8139_rx_phys);
    if (g_rtl8139_rx_buffer == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < RTL8139_RX_BUFFER_BYTES; i++) {
        g_rtl8139_rx_buffer[i] = 0;
    }
    return 1;
}

static int rtl8139_wait_tx_complete(uint16_t io_base) {
    uint32_t slot = g_rtl8139_tx_slot;
    uint32_t spins = 0;
    uint16_t tsd_reg = (uint16_t)(RTL8139_REG_TSD0 + slot * 4u);

    while (spins < 1000000u) {
        uint32_t status = inl((uint16_t)(io_base + tsd_reg));

        if ((status & RTL8139_TSD_TOK) != 0u) {
            return 1;
        }
        if ((status & (RTL8139_TSD_TABT | RTL8139_TSD_TUN)) != 0u) {
            return 0;
        }
        spins++;
    }
    return 0;
}

static uint16_t rtl8139_read_u16(const uint8_t *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

int rtl8139_init(void) {
    struct pci_device_info device;
    uint16_t command;
    uint16_t io_base;

    g_rtl8139_status.present = 0;
    g_rtl8139_status.initialized = 0;

    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &device)) {
        return 0;
    }

    g_rtl8139_status.present = 1;
    g_rtl8139_status.bus = device.bus;
    g_rtl8139_status.slot = device.slot;
    g_rtl8139_status.function = device.function;
    g_rtl8139_status.prog_if = device.prog_if;
    g_rtl8139_status.irq_line = device.irq_line;
    g_rtl8139_status.irq_pin = device.irq_pin;
    g_rtl8139_status.vendor_id = device.vendor_id;
    g_rtl8139_status.device_id = device.device_id;
    io_base = rtl8139_io_base_from_bar(device.bar0);
    g_rtl8139_status.io_base = io_base;
    if (io_base == 0u) {
        return 1;
    }

    command = pci_config_read16(device.bus, device.slot, device.function, 0x04);
    command |= (uint16_t)(PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(device.bus, device.slot, device.function, 0x04, command);
    g_rtl8139_status.pci_command = pci_config_read16(device.bus, device.slot, device.function, 0x04);

    outb((uint16_t)(io_base + RTL8139_REG_CONFIG1), 0x00u);
    outb((uint16_t)(io_base + RTL8139_REG_CHIPCMD), RTL8139_CHIPCMD_RST);
    if (!rtl8139_wait_reset_clear(io_base)) {
        rtl8139_refresh_runtime_status();
        return 1;
    }

    if (!rtl8139_prepare_rx_buffer()) {
        rtl8139_refresh_runtime_status();
        return 1;
    }

    g_rtl8139_rx_offset = 0u;
    outl((uint16_t)(io_base + RTL8139_REG_RBSTART), (uint32_t)g_rtl8139_rx_phys);
    outw((uint16_t)(io_base + RTL8139_REG_CAPR), 0u);
    outw((uint16_t)(io_base + RTL8139_REG_ISR), 0xffffu);
    outw((uint16_t)(io_base + RTL8139_REG_IMR), RTL8139_IRQ_MASK_DEFAULT);
    outl((uint16_t)(io_base + RTL8139_REG_RCR),
         RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB | RTL8139_RCR_WRAP);
    outb((uint16_t)(io_base + RTL8139_REG_CHIPCMD), RTL8139_CHIPCMD_RE | RTL8139_CHIPCMD_TE);
    if (g_rtl8139_status.irq_line < 16u) {
        hal_irq_set_mask(g_rtl8139_status.irq_line, 0);
    }
    rtl8139_refresh_runtime_status();
    g_rtl8139_status.initialized = 1;
    rtl8139_emit_status_event_if_changed();
    return 1;
}

int rtl8139_query_status(struct rtl8139_status *out) {
    if (out == 0) {
        return 0;
    }

    rtl8139_refresh_runtime_status();
    *out = g_rtl8139_status;
    return g_rtl8139_status.present != 0;
}

int rtl8139_send_frame(const uint8_t *data, uint32_t bytes) {
    uint16_t io_base = g_rtl8139_status.io_base;
    uint32_t frame_size;
    uint32_t slot;
    uint16_t tsad_reg;
    uint16_t tsd_reg;

    if (!g_rtl8139_status.present || !g_rtl8139_status.initialized || io_base == 0u || data == 0) {
        return 0;
    }
    if (bytes < 14u || bytes > RTL8139_TX_BUFFER_BYTES) {
        return 0;
    }
    if (!rtl8139_prepare_tx_buffer()) {
        return 0;
    }

    frame_size = bytes < 60u ? 60u : bytes;
    slot = g_rtl8139_tx_slot;
    tsad_reg = (uint16_t)(RTL8139_REG_TSAD0 + slot * 4u);
    tsd_reg = (uint16_t)(RTL8139_REG_TSD0 + slot * 4u);
    rtl8139_copy_frame(g_rtl8139_tx_buffer[slot], data, bytes);
    outw((uint16_t)(io_base + RTL8139_REG_ISR), RTL8139_ISR_TOK | RTL8139_ISR_TER);
    outl((uint16_t)(io_base + tsad_reg), (uint32_t)g_rtl8139_tx_phys[slot]);
    outl((uint16_t)(io_base + tsd_reg), frame_size & RTL8139_TSD_SIZE_MASK);
    if (!rtl8139_wait_tx_complete(io_base)) {
        rtl8139_refresh_runtime_status();
        return 0;
    }

    g_rtl8139_tx_slot = (slot + 1u) % RTL8139_TX_DESC_COUNT;
    rtl8139_refresh_runtime_status();
    return 1;
}

int rtl8139_send_test_frame(void) {
    uint8_t frame[60];

    rtl8139_fill_test_frame(frame, sizeof(frame));
    return rtl8139_send_frame(frame, sizeof(frame));
}

int rtl8139_receive_packet(struct rtl8139_rx_packet *out) {
    uint16_t io_base = g_rtl8139_status.io_base;
    uint16_t packet_status;
    uint16_t packet_length;
    uint32_t payload_length;
    uint32_t next_offset;
    uint32_t bytes_copied;
    uint32_t i;
    const uint8_t *src;

    if (out == 0) {
        return 0;
    }
    out->packet_status = 0u;
    out->packet_length = 0u;
    out->bytes_copied = 0u;
    for (i = 0; i < sizeof(out->data); i++) {
        out->data[i] = 0u;
    }

    if (!g_rtl8139_status.present || !g_rtl8139_status.initialized || io_base == 0u || g_rtl8139_rx_buffer == 0) {
        return 0;
    }
    if ((inb((uint16_t)(io_base + RTL8139_REG_CHIPCMD)) & RTL8139_RX_BUFFER_EMPTY) != 0u) {
        rtl8139_refresh_runtime_status();
        return 0;
    }

    src = g_rtl8139_rx_buffer + g_rtl8139_rx_offset;
    packet_status = rtl8139_read_u16(src);
    packet_length = rtl8139_read_u16(src + 2u);
    out->packet_status = packet_status;
    out->packet_length = packet_length;
    if ((packet_status & RTL8139_RX_STATUS_ROK) == 0u || packet_length < 4u) {
        rtl8139_refresh_runtime_status();
        return 0;
    }

    payload_length = (uint32_t)packet_length - 4u;
    bytes_copied = payload_length < sizeof(out->data) ? payload_length : sizeof(out->data);
    src += 4u;
    for (i = 0; i < bytes_copied; i++) {
        out->data[i] = src[i];
    }
    out->bytes_copied = bytes_copied;

    next_offset = (g_rtl8139_rx_offset + (uint32_t)packet_length + 4u + 3u) & ~3u;
    g_rtl8139_rx_offset = next_offset & RTL8139_RX_RING_MASK;
    outw((uint16_t)(io_base + RTL8139_REG_CAPR), (uint16_t)((g_rtl8139_rx_offset - 16u) & RTL8139_RX_RING_MASK));
    rtl8139_refresh_runtime_status();
    return 1;
}

int rtl8139_handle_irq(uint8_t irq_line) {
    uint16_t io_base = g_rtl8139_status.io_base;
    uint16_t status;

    if (!g_rtl8139_status.present || !g_rtl8139_status.initialized || io_base == 0u) {
        return 0;
    }
    if (g_rtl8139_status.irq_line != irq_line) {
        return 0;
    }

    status = inw((uint16_t)(io_base + RTL8139_REG_ISR));
    if (status == 0u || status == 0xffffu) {
        return 0;
    }

    outw((uint16_t)(io_base + RTL8139_REG_ISR), status);
    rtl8139_refresh_runtime_status();
    return 1;
}
