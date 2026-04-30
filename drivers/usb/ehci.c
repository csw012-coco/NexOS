#include "drivers/usb/ehci.h"

#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/input/keyboard.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

enum {
    EHCI_MAX_PORTS = 16u,
    EHCI_MAX_MSC = 2u,
    EHCI_MAX_HUBS = 2u,
    EHCI_MAX_HID_KEYBOARDS = 2u,
    EHCI_HID_EVENT_QUEUE_SIZE = 16u,
    EHCI_MSC_DEBUG = 0u,
    EHCI_PAGE_SIZE = 4096u,
    EHCI_SECTOR_SIZE = 512u,
    EHCI_MSC_READAHEAD_MAX_SECTORS = 1u,
    EHCI_MSC_READAHEAD_INITIAL_SECTORS = 1u,
    EHCI_ASYNC_CONTROL_SPINS = 8000000u,
    EHCI_ASYNC_BULK_META_SPINS = 12000000u,
    EHCI_ASYNC_BULK_DATA_SPINS = 300000000u,
    EHCI_ASYNC_BULK_CSW_SPINS = 12000000u,
    EHCI_MSC_CBW_STAGE_SETTLE_MS = 5u,
    EHCI_MSC_DATA_CSW_SETTLE_MS = 15u,
    EHCI_MSC_RESET_SETTLE_MS = 250u,
    EHCI_SET_CONFIGURATION_SETTLE_MS = 200u,
    EHCI_PIT_RATE_HZ = 1193182u,

    EHCI_USBCMD_RS = 1u << 0,
    EHCI_USBCMD_HCRESET = 1u << 1,
    EHCI_USBCMD_ASE = 1u << 5,

    EHCI_USBSTS_CLEAR = 0x3fu,
    EHCI_USBSTS_HCHALTED = 1u << 12,
    EHCI_USBSTS_ASS = 1u << 15,

    EHCI_PORT_CONNECT = 1u << 0,
    EHCI_PORT_CONNECT_CHANGE = 1u << 1,
    EHCI_PORT_ENABLE = 1u << 2,
    EHCI_PORT_ENABLE_CHANGE = 1u << 3,
    EHCI_PORT_OVER_CURRENT_CHANGE = 1u << 5,
    EHCI_PORT_RESET = 1u << 8,
    EHCI_PORT_POWER = 1u << 12,
    EHCI_PORT_OWNER = 1u << 13,
    EHCI_PORT_WRITE_CLEAR_BITS = EHCI_PORT_CONNECT_CHANGE |
                                 EHCI_PORT_ENABLE_CHANGE |
                                 EHCI_PORT_OVER_CURRENT_CHANGE,

    EHCI_LINK_TERMINATE = 1u,
    EHCI_LINK_TYPE_QH = 1u << 1,

    EHCI_QH_DTC = 1u << 14,
    EHCI_QH_HEAD = 1u << 15,
    EHCI_QH_EPS_FULL = 0u << 12,
    EHCI_QH_EPS_LOW = 1u << 12,
    EHCI_QH_EPS_HIGH = 2u << 12,
    EHCI_QH_CONTROL_EP = 1u << 27,
    EHCI_QH_RL_HS = 4u << 28,
    EHCI_QH_MULT_1 = 1u << 30,

    EHCI_DEV_SPEED_FULL = 0u,
    EHCI_DEV_SPEED_LOW = 1u,
    EHCI_DEV_SPEED_HIGH = 2u,

    EHCI_QTD_PID_OUT = 0u,
    EHCI_QTD_PID_IN = 1u,
    EHCI_QTD_PID_SETUP = 2u,
    EHCI_QTD_ACTIVE = 1u << 7,
    EHCI_QTD_HALTED = 1u << 6,
    EHCI_QTD_CERR_SHIFT = 10u,
    EHCI_QTD_IOC = 1u << 15,
    EHCI_QTD_TOGGLE = 1u << 31,

    USB_REQ_GET_STATUS = 0x00u,
    USB_REQ_CLEAR_FEATURE = 0x01u,
    USB_REQ_BULK_ONLY_RESET = 0xffu,
    USB_REQ_GET_DESCRIPTOR = 0x06u,
    USB_REQ_SET_ADDRESS = 0x05u,
    USB_REQ_SET_CONFIGURATION = 0x09u,
    USB_REQ_SET_FEATURE = 0x03u,
    USB_REQ_GET_MAX_LUN = 0xfeu,
    USB_REQ_GET_REPORT = 0x01u,
    USB_REQ_SET_IDLE = 0x0au,
    USB_REQ_SET_PROTOCOL = 0x0bu,
    USB_DESC_DEVICE = 0x01u,
    USB_DESC_CONFIGURATION = 0x02u,
    USB_DESC_HUB = 0x29u,

    USB_CLASS_HID = 0x03u,
    USB_CLASS_HUB = 0x09u,
    USB_SUBCLASS_BOOT = 0x01u,
    USB_PROTO_KEYBOARD = 0x01u,
    USB_CLASS_MASS_STORAGE = 0x08u,
    USB_SUBCLASS_SCSI = 0x06u,
    USB_PROTO_BULK_ONLY = 0x50u,
    USB_FEATURE_ENDPOINT_HALT = 0u,
    USB_MSC_MAX_LUN_LIMIT = 15u,
    EHCI_MSC_RW_RETRIES = 3u,

    USB_HUB_PORT_CONNECTION = 1u << 0,
    USB_HUB_PORT_ENABLE = 1u << 1,
    USB_HUB_PORT_RESET = 1u << 4,
    USB_HUB_PORT_POWER = 1u << 8,
    USB_HUB_PORT_LOW_SPEED = 1u << 9,
    USB_HUB_PORT_HIGH_SPEED = 1u << 10,
    USB_HUB_FEATURE_PORT_RESET = 4u,
    USB_HUB_FEATURE_PORT_POWER = 8u,
    USB_HUB_FEATURE_C_PORT_CONNECTION = 16u,
    USB_HUB_FEATURE_C_PORT_ENABLE = 17u,
    USB_HUB_FEATURE_C_PORT_RESET = 20u,

    MSC_CBW_SIGNATURE = 0x43425355u,
    MSC_CSW_SIGNATURE = 0x53425355u,
    MSC_STATUS_TRANSPORT_ERROR = 0xffu,
    SCSI_TEST_UNIT_READY = 0x00u,
    SCSI_REQUEST_SENSE = 0x03u,
    SCSI_INQUIRY = 0x12u,
    SCSI_READ_CAPACITY_10 = 0x25u,
    SCSI_READ_10 = 0x28u,
    SCSI_WRITE_10 = 0x2au,
    SCSI_SYNCHRONIZE_CACHE_10 = 0x35u
};

struct ehci_qtd {
    volatile uint32_t next;
    volatile uint32_t alt_next;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
    uint32_t pad[3];
};

struct ehci_qh {
    volatile uint32_t horiz_link;
    volatile uint32_t ep_char;
    volatile uint32_t ep_cap;
    volatile uint32_t current_qtd;
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
};

struct ehci_regs {
    volatile uint8_t *cap;
    volatile uint32_t *op;
    uint8_t cap_length;
    uint8_t port_count;
};

struct usb_ctrl_request {
    uint8_t type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

struct ehci_msc_device {
    char name[12];
    struct ehci_regs controller;
    uint8_t present;
    uint8_t address;
    uint8_t configuration;
    uint8_t speed;
    uint8_t root_port;
    uint8_t hub_addr;
    uint8_t hub_port;
    uint8_t msc_interface_number;
    uint8_t msc_lun;
    uint8_t max_lun;
    uint8_t read_cache_valid;
    uint8_t read_cache_count;
    uint8_t read_ahead_sectors;
    uint8_t last_msc_phase;
    uint8_t last_msc_status;
    uint8_t last_sense_key;
    uint8_t last_sense_asc;
    uint8_t last_sense_ascq;
    uint8_t sync_cache_supported;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint16_t control_mps;
    uint16_t bulk_in_mps;
    uint16_t bulk_out_mps;
    uint8_t bulk_in_toggle;
    uint8_t bulk_out_toggle;
    uint32_t tag;
    uint32_t last_msc_residue;
    uint64_t sector_count;
    uint64_t read_cache_lba;
    uint64_t qh_phys;
    uint64_t qtd_phys;
    uint64_t setup_phys;
    uint64_t data_phys;
    uint64_t read_cache_phys;
    uint64_t cbw_phys;
    uint64_t csw_phys;
    struct ehci_qh *qh;
    struct ehci_qtd *qtd;
    uint8_t *setup;
    uint8_t *data;
    uint8_t *read_cache;
    uint8_t *cbw;
    uint8_t *csw;
    struct block_device blockdev;
};

struct ehci_hid_keyboard {
    uint8_t present;
    uint8_t address;
    uint8_t configuration;
    uint8_t interface_number;
    uint8_t interrupt_in_ep;
    uint8_t report_fail_logged;
    uint16_t interrupt_in_mps;
    uint8_t last_report[8];
    struct ehci_msc_device xfer;
};

static struct ehci_regs g_ehci;
static struct ehci_msc_device g_msc[EHCI_MAX_MSC];
static uint32_t g_msc_count;
static struct ehci_msc_device g_hubs[EHCI_MAX_HUBS];
static uint32_t g_hub_count;
static struct ehci_hid_keyboard g_hid_keyboards[EHCI_MAX_HID_KEYBOARDS];
static uint32_t g_hid_keyboard_count;
static uint8_t g_next_address;
static struct keyboard_event g_hid_event_queue[EHCI_HID_EVENT_QUEUE_SIZE];
static uint32_t g_hid_event_head;
static uint32_t g_hid_event_tail;
static uint32_t g_hid_event_count;
static uint32_t g_hid_poll_divider;
static uint64_t g_async_head_phys;
static uint64_t g_async_dummy_qtd_phys;
static uint64_t g_periodic_list_phys;
static struct ehci_qh *g_async_head;
static struct ehci_qtd *g_async_dummy_qtd;
static uint32_t *g_periodic_list;

static int ehci_finish_device_enumeration(struct ehci_msc_device *dev,
                                          uint32_t root_port,
                                          uint8_t *dev_desc,
                                          uint16_t mps0);
static int ehci_probe_hub(struct ehci_msc_device *dev,
                          uint32_t root_port,
                          uint16_t mps0,
                          const uint8_t *cfg,
                          uint32_t cfg_len);
static int ehci_clear_endpoint_halt(struct ehci_msc_device *dev, uint8_t ep);
static int ehci_msc_buffer_has_transport_signature(const uint8_t *data);
static int ehci_hub_set_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature);
static int ehci_hub_get_port_status(struct ehci_msc_device *hub,
                                    uint8_t port,
                                    uint16_t *status_out,
                                    uint16_t *change_out);
static void ehci_hub_clear_changes(struct ehci_msc_device *hub, uint8_t port, uint16_t change);
static uint8_t ehci_hub_child_speed(uint16_t status);

static uint8_t ehci_read8(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

static void ehci_use_device_controller(const struct ehci_msc_device *dev) {
    if (dev != 0 && dev->controller.op != 0) {
        g_ehci = dev->controller;
    }
}

static uint32_t ehci_read_cap32(uint32_t offset) {
    return *(volatile uint32_t *)(g_ehci.cap + offset);
}

static uint32_t ehci_read_op(uint32_t offset) {
    return g_ehci.op[offset / 4u];
}

static void ehci_write_op(uint32_t offset, uint32_t value) {
    g_ehci.op[offset / 4u] = value;
}

static void ehci_delay_spin(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

static void ehci_delay_ms(uint32_t ms) {
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

static void ehci_dma_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static int ehci_wait_op_clear(uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((ehci_read_op(offset) & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ehci_wait_op_set(uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((ehci_read_op(offset) & mask) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int ehci_start_controller(void) {
    ehci_write_op(0x04u, EHCI_USBSTS_CLEAR);
    ehci_write_op(0x08u, 0u);
    ehci_write_op(0x10u, 0u);
    if (g_periodic_list_phys != 0u) {
        ehci_write_op(0x14u, (uint32_t)g_periodic_list_phys);
    }
    if (g_async_head_phys != 0u) {
        ehci_write_op(0x18u, (uint32_t)g_async_head_phys);
    }
    (void)ehci_read_op(0x18u);
    ehci_write_op(0x00u, EHCI_USBCMD_RS);
    (void)ehci_read_op(0x00u);
    if (!ehci_wait_op_clear(0x04u, EHCI_USBSTS_HCHALTED)) {
        return 0;
    }
    ehci_write_op(0x40u, 1u);
    (void)ehci_read_op(0x40u);
    ehci_delay_ms(5u);
    return ehci_wait_op_clear(0x04u, EHCI_USBSTS_HCHALTED);
}

static int ehci_reset_controller_runtime(void) {
    ehci_write_op(0x00u, ehci_read_op(0x00u) & ~(EHCI_USBCMD_RS | EHCI_USBCMD_ASE));
    (void)ehci_wait_op_set(0x04u, EHCI_USBSTS_HCHALTED);
    ehci_write_op(0x00u, EHCI_USBCMD_HCRESET);
    if (!ehci_wait_op_clear(0x00u, EHCI_USBCMD_HCRESET)) {
        return 0;
    }
    return ehci_start_controller();
}

static int ehci_ensure_running(void) {
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

static uint32_t ehci_port_write_value(uint32_t port) {
    return port & ~EHCI_PORT_WRITE_CLEAR_BITS;
}

static int ehci_reset_port(uint32_t port_index, uint32_t *port_out) {
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

static uint16_t ehci_read_u16le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t ehci_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t ehci_read_u32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void ehci_write_u32le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void ehci_write_u16le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void ehci_write_u32be(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)((value >> 24) & 0xffu);
    data[1] = (uint8_t)((value >> 16) & 0xffu);
    data[2] = (uint8_t)((value >> 8) & 0xffu);
    data[3] = (uint8_t)(value & 0xffu);
}

static void ehci_write_name(char *dst, uint32_t index) {
    dst[0] = 'u';
    dst[1] = 's';
    dst[2] = 'b';
    dst[3] = 'm';
    dst[4] = 's';
    dst[5] = 'c';
    dst[6] = (char)('0' + index);
    dst[7] = '\0';
}

static int ehci_alloc_async_head(void) {
    g_async_head_phys = pmm_alloc_page();
    g_async_dummy_qtd_phys = pmm_alloc_page();
    g_periodic_list_phys = pmm_alloc_page();
    if (g_async_head_phys == 0u || g_async_dummy_qtd_phys == 0u ||
        g_periodic_list_phys == 0u ||
        g_async_head_phys > 0xffffffffull || g_async_dummy_qtd_phys > 0xffffffffull ||
        g_periodic_list_phys > 0xffffffffull) {
        return 0;
    }
    g_async_head = (struct ehci_qh *)hal_phys_direct_map(g_async_head_phys);
    g_async_dummy_qtd = (struct ehci_qtd *)hal_phys_direct_map(g_async_dummy_qtd_phys);
    g_periodic_list = (uint32_t *)hal_phys_direct_map(g_periodic_list_phys);
    if (g_async_head == 0 || g_async_dummy_qtd == 0 || g_periodic_list == 0) {
        return 0;
    }
    memset(g_async_head, 0, EHCI_PAGE_SIZE);
    memset(g_async_dummy_qtd, 0, EHCI_PAGE_SIZE);
    for (uint32_t i = 0; i < EHCI_PAGE_SIZE / sizeof(uint32_t); i++) {
        g_periodic_list[i] = EHCI_LINK_TERMINATE;
    }
    g_async_dummy_qtd->next = EHCI_LINK_TERMINATE;
    g_async_dummy_qtd->alt_next = EHCI_LINK_TERMINATE;
    g_async_dummy_qtd->token = EHCI_QTD_HALTED;
    return 1;
}

static int ehci_alloc_msc_memory(struct ehci_msc_device *dev) {
    dev->qh_phys = pmm_alloc_page();
    dev->qtd_phys = pmm_alloc_page();
    dev->setup_phys = pmm_alloc_page();
    dev->data_phys = pmm_alloc_page();
    dev->read_cache_phys = pmm_alloc_page();
    dev->cbw_phys = pmm_alloc_page();
    dev->csw_phys = pmm_alloc_page();
    if (dev->qh_phys == 0u || dev->qtd_phys == 0u || dev->setup_phys == 0u ||
        dev->data_phys == 0u || dev->read_cache_phys == 0u ||
        dev->cbw_phys == 0u || dev->csw_phys == 0u ||
        dev->qh_phys > 0xffffffffull || dev->qtd_phys > 0xffffffffull ||
        dev->setup_phys > 0xffffffffull || dev->data_phys > 0xffffffffull ||
        dev->read_cache_phys > 0xffffffffull ||
        dev->cbw_phys > 0xffffffffull || dev->csw_phys > 0xffffffffull) {
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
        return 0;
    }
    memset(dev->qh, 0, EHCI_PAGE_SIZE);
    memset(dev->qtd, 0, EHCI_PAGE_SIZE);
    memset(dev->setup, 0, EHCI_PAGE_SIZE);
    memset(dev->data, 0, EHCI_PAGE_SIZE);
    memset(dev->read_cache, 0, EHCI_PAGE_SIZE);
    memset(dev->cbw, 0, EHCI_PAGE_SIZE);
    memset(dev->csw, 0, EHCI_PAGE_SIZE);
    dev->read_cache_valid = 0u;
    dev->read_cache_count = 0u;
    dev->read_ahead_sectors = EHCI_MSC_READAHEAD_INITIAL_SECTORS;
    dev->sync_cache_supported = 1u;
    return 1;
}

static void ehci_qtd_prepare(struct ehci_qtd *qtd,
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

static uint32_t ehci_qh_speed_bits(uint8_t speed) {
    if (speed == EHCI_DEV_SPEED_LOW) {
        return EHCI_QH_EPS_LOW;
    }
    if (speed == EHCI_DEV_SPEED_HIGH) {
        return EHCI_QH_EPS_HIGH;
    }
    return EHCI_QH_EPS_FULL;
}

static void ehci_qh_prepare(struct ehci_msc_device *dev,
                            uint8_t addr,
                            uint8_t ep,
                            uint16_t mps,
                            uint8_t control,
                            uint32_t first_qtd_phys,
                            uint8_t qtd_controls_toggle,
                            uint8_t initial_toggle) {
    uint8_t active_is_head = g_async_head == 0 ? 1u : 0u;
    uint8_t speed = dev->speed;

    memset(dev->qh, 0, sizeof(*dev->qh));
    if (speed > EHCI_DEV_SPEED_HIGH) {
        speed = EHCI_DEV_SPEED_HIGH;
    }
    dev->qh->horiz_link = (g_async_head != 0 ? (uint32_t)g_async_head_phys : (uint32_t)dev->qh_phys) |
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

static void ehci_async_head_prepare(struct ehci_msc_device *dev) {
    if (g_async_head == 0 || dev == 0) {
        return;
    }
    memset(g_async_head, 0, sizeof(*g_async_head));
    g_async_head->horiz_link = (uint32_t)dev->qh_phys | EHCI_LINK_TYPE_QH;
    g_async_head->ep_char = EHCI_QH_HEAD;
    g_async_head->ep_cap = 0u;
    g_async_head->current_qtd = 0u;
    g_async_head->next_qtd = EHCI_LINK_TERMINATE;
    g_async_head->alt_next_qtd = g_async_dummy_qtd_phys != 0u
                                 ? (uint32_t)g_async_dummy_qtd_phys
                                 : EHCI_LINK_TERMINATE;
    g_async_head->token = EHCI_QTD_HALTED;
}

static int ehci_run_async_transfer(struct ehci_msc_device *dev,
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
    async_head_phys = g_async_head != 0 ? (uint32_t)g_async_head_phys : (uint32_t)dev->qh_phys;
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
    }
    ehci_write_op(0x00u, ehci_read_op(0x00u) & ~EHCI_USBCMD_ASE);
    (void)ehci_wait_op_clear(0x04u, EHCI_USBSTS_ASS);
    return 0;
}

static void ehci_log_transfer_state(const char *label,
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
    if (g_async_head != 0) {
        kprint("ehci: %s async=%x head_next=%x head_info=%x head_qtd=%x head_alt=%x head_token=%x\n",
               label,
               ehci_read_op(0x18u),
               g_async_head->horiz_link,
               g_async_head->ep_char,
               g_async_head->next_qtd,
               g_async_head->alt_next_qtd,
               g_async_head->token);
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

static int ehci_control_transfer(struct ehci_msc_device *dev,
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
    ehci_write_u16le(dev->setup + 2, req->value);
    ehci_write_u16le(dev->setup + 4, req->index);
    ehci_write_u16le(dev->setup + 6, req->length);
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

static int ehci_bulk_transfer(struct ehci_msc_device *dev,
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

    if (dev == 0 || toggle == 0 || bytes > EHCI_PAGE_SIZE || mps == 0u) {
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
               (uint32_t)g_async_head_phys,
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

static int ehci_get_descriptor(struct ehci_msc_device *dev,
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

static int ehci_get_initial_device_descriptor(struct ehci_msc_device *dev,
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

static int ehci_set_address(struct ehci_msc_device *dev, uint8_t address, uint16_t mps) {
    struct usb_ctrl_request req;

    req.type = 0x00u;
    req.request = USB_REQ_SET_ADDRESS;
    req.value = address;
    req.index = 0u;
    req.length = 0u;
    return ehci_control_transfer(dev, 0u, mps, &req, 0, 0u, 0u);
}

static int ehci_set_configuration(struct ehci_msc_device *dev, uint8_t configuration, uint16_t mps) {
    struct usb_ctrl_request req;

    req.type = 0x00u;
    req.request = USB_REQ_SET_CONFIGURATION;
    req.value = configuration;
    req.index = 0u;
    req.length = 0u;
    return ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
}

static void ehci_log_config_descriptor(const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0u;

    if (cfg == 0 || length < 9u) {
        return;
    }
    kprint("ehci: cfg total=%u value=%u ifaces=%u attrs=%x maxpwr=%u\n",
           (uint32_t)ehci_read_u16le(cfg + 2),
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
                   (uint32_t)ehci_read_u16le(cfg + offset + 4u),
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

static int ehci_parse_msc_config(struct ehci_msc_device *dev, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_msc = 0;
    uint8_t tmp_ep;
    uint16_t tmp_mps;

    if (length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
    dev->msc_interface_number = 0u;
    dev->bulk_in_ep = 0u;
    dev->bulk_out_ep = 0u;
    dev->bulk_in_mps = 0u;
    dev->bulk_out_mps = 0u;
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            in_msc = cfg[offset + 5u] == USB_CLASS_MASS_STORAGE &&
                     cfg[offset + 6u] == USB_SUBCLASS_SCSI &&
                     cfg[offset + 7u] == USB_PROTO_BULK_ONLY;
            if (in_msc) {
                dev->msc_interface_number = cfg[offset + 2u];
            }
        } else if (type == 5u && len >= 7u && in_msc) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = ehci_read_u16le(cfg + offset + 4u);

            if (attr == 2u && (ep & 0x80u) != 0u) {
                dev->bulk_in_ep = ep;
                dev->bulk_in_mps = mps;
            } else if (attr == 2u) {
                dev->bulk_out_ep = ep;
                dev->bulk_out_mps = mps;
            }
        }
        offset += len;
    }
    if (dev->configuration == 0u ||
        dev->bulk_in_ep == 0u ||
        dev->bulk_out_ep == 0u ||
        dev->bulk_in_mps == 0u ||
        dev->bulk_out_mps == 0u) {
        kprint("ehci: msc parsed incomplete in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
        return 0;
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: msc parsed raw in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
    }
    if ((dev->bulk_in_ep & 0x80u) == 0u && (dev->bulk_out_ep & 0x80u) != 0u) {
        tmp_ep = dev->bulk_in_ep;
        tmp_mps = dev->bulk_in_mps;
        dev->bulk_in_ep = dev->bulk_out_ep;
        dev->bulk_in_mps = dev->bulk_out_mps;
        dev->bulk_out_ep = tmp_ep;
        dev->bulk_out_mps = tmp_mps;
        kprint("ehci: msc EP direction swapped in=%x out=%x\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_out_ep);
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: msc parsed in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
    }
    return 1;
}

static int ehci_parse_hid_keyboard_config(struct ehci_hid_keyboard *kbd, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_keyboard = 0;

    if (kbd == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    kbd->configuration = cfg[5];
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            in_keyboard = cfg[offset + 5u] == USB_CLASS_HID &&
                          cfg[offset + 6u] == USB_SUBCLASS_BOOT &&
                          cfg[offset + 7u] == USB_PROTO_KEYBOARD;
            if (in_keyboard) {
                kbd->interface_number = cfg[offset + 2u];
            }
        } else if (type == 5u && len >= 7u && in_keyboard) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = ehci_read_u16le(cfg + offset + 4u);

            if (attr == 3u && (ep & 0x80u) != 0u) {
                kbd->interrupt_in_ep = ep;
                kbd->interrupt_in_mps = mps;
            }
        }
        offset += len;
    }
    return kbd->configuration != 0u && kbd->interrupt_in_ep != 0u && kbd->interrupt_in_mps != 0u;
}

static int ehci_hid_set_protocol(struct ehci_hid_keyboard *kbd, uint8_t protocol) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_PROTOCOL;
    req.value = protocol;
    req.index = kbd->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

static int ehci_hid_set_idle(struct ehci_hid_keyboard *kbd) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_IDLE;
    req.value = 0u;
    req.index = kbd->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

static int ehci_hid_get_report(struct ehci_hid_keyboard *kbd, uint8_t report[8]) {
    struct usb_ctrl_request req;

    req.type = 0xa1u;
    req.request = USB_REQ_GET_REPORT;
    req.value = 0x0100u;
    req.index = kbd->interface_number;
    req.length = 8u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, report, 8u, 1u);
}

static enum keyboard_keycode ehci_hid_usage_to_keycode(uint8_t usage) {
    static const enum keyboard_keycode usage_map[256] = {
        [0x04] = KEYBOARD_KEY_A, [0x05] = KEYBOARD_KEY_B, [0x06] = KEYBOARD_KEY_C,
        [0x07] = KEYBOARD_KEY_D, [0x08] = KEYBOARD_KEY_E, [0x09] = KEYBOARD_KEY_F,
        [0x0a] = KEYBOARD_KEY_G, [0x0b] = KEYBOARD_KEY_H, [0x0c] = KEYBOARD_KEY_I,
        [0x0d] = KEYBOARD_KEY_J, [0x0e] = KEYBOARD_KEY_K, [0x0f] = KEYBOARD_KEY_L,
        [0x10] = KEYBOARD_KEY_M, [0x11] = KEYBOARD_KEY_N, [0x12] = KEYBOARD_KEY_O,
        [0x13] = KEYBOARD_KEY_P, [0x14] = KEYBOARD_KEY_Q, [0x15] = KEYBOARD_KEY_R,
        [0x16] = KEYBOARD_KEY_S, [0x17] = KEYBOARD_KEY_T, [0x18] = KEYBOARD_KEY_U,
        [0x19] = KEYBOARD_KEY_V, [0x1a] = KEYBOARD_KEY_W, [0x1b] = KEYBOARD_KEY_X,
        [0x1c] = KEYBOARD_KEY_Y, [0x1d] = KEYBOARD_KEY_Z,
        [0x1e] = KEYBOARD_KEY_1, [0x1f] = KEYBOARD_KEY_2, [0x20] = KEYBOARD_KEY_3,
        [0x21] = KEYBOARD_KEY_4, [0x22] = KEYBOARD_KEY_5, [0x23] = KEYBOARD_KEY_6,
        [0x24] = KEYBOARD_KEY_7, [0x25] = KEYBOARD_KEY_8, [0x26] = KEYBOARD_KEY_9,
        [0x27] = KEYBOARD_KEY_0,
        [0x28] = KEYBOARD_KEY_ENTER,
        [0x29] = KEYBOARD_KEY_ESC,
        [0x2a] = KEYBOARD_KEY_BACKSPACE,
        [0x2b] = KEYBOARD_KEY_TAB,
        [0x2c] = KEYBOARD_KEY_SPACE,
        [0x2d] = KEYBOARD_KEY_MINUS,
        [0x2e] = KEYBOARD_KEY_EQUAL,
        [0x2f] = KEYBOARD_KEY_LEFT_BRACKET,
        [0x30] = KEYBOARD_KEY_RIGHT_BRACKET,
        [0x31] = KEYBOARD_KEY_BACKSLASH,
        [0x33] = KEYBOARD_KEY_SEMICOLON,
        [0x34] = KEYBOARD_KEY_APOSTROPHE,
        [0x35] = KEYBOARD_KEY_GRAVE,
        [0x36] = KEYBOARD_KEY_COMMA,
        [0x37] = KEYBOARD_KEY_PERIOD,
        [0x38] = KEYBOARD_KEY_SLASH,
        [0x39] = KEYBOARD_KEY_CAPS_LOCK,
        [0x4a] = KEYBOARD_KEY_HOME,
        [0x4b] = KEYBOARD_KEY_PAGE_UP,
        [0x4c] = KEYBOARD_KEY_DELETE,
        [0x4d] = KEYBOARD_KEY_END,
        [0x4e] = KEYBOARD_KEY_PAGE_DOWN,
        [0x4f] = KEYBOARD_KEY_RIGHT,
        [0x50] = KEYBOARD_KEY_LEFT,
        [0x51] = KEYBOARD_KEY_DOWN,
        [0x52] = KEYBOARD_KEY_UP,
        [0xe0] = KEYBOARD_KEY_LEFT_CTRL,
        [0xe1] = KEYBOARD_KEY_LEFT_SHIFT,
        [0xe4] = KEYBOARD_KEY_RIGHT_CTRL,
        [0xe5] = KEYBOARD_KEY_RIGHT_SHIFT
    };

    return usage_map[usage];
}

static int ehci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage) {
    for (uint32_t i = 2; i < 8u; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void ehci_hid_queue_event(struct keyboard_event event) {
    if (event.keycode == KEYBOARD_KEY_NONE) {
        return;
    }
    if (g_hid_event_count >= EHCI_HID_EVENT_QUEUE_SIZE) {
        g_hid_event_tail = (g_hid_event_tail + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
        g_hid_event_count--;
    }
    g_hid_event_queue[g_hid_event_head] = event;
    g_hid_event_head = (g_hid_event_head + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count++;
}

static int ehci_hid_pop_event(struct keyboard_event *out) {
    if (out == 0 || g_hid_event_count == 0u) {
        return 0;
    }
    *out = g_hid_event_queue[g_hid_event_tail];
    g_hid_event_tail = (g_hid_event_tail + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count--;
    return 1;
}

static void ehci_hid_process_report(struct ehci_hid_keyboard *kbd, const uint8_t report[8]) {
    static const uint8_t modifier_usages[8] = {0xe0u, 0xe1u, 0xe2u, 0xe3u, 0xe4u, 0xe5u, 0xe6u, 0xe7u};

    for (uint32_t i = 0; i < 8u; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        enum keyboard_keycode keycode = ehci_hid_usage_to_keycode(modifier_usages[i]);

        if (((kbd->last_report[0] ^ report[0]) & mask) == 0u) {
            continue;
        }
        ehci_hid_queue_event(keyboard_handle_keycode(keycode, (report[0] & mask) != 0u));
    }
    for (uint32_t i = 2; i < 8u; i++) {
        uint8_t usage = kbd->last_report[i];

        if (usage != 0u && !ehci_hid_report_contains_usage(report, usage)) {
            ehci_hid_queue_event(keyboard_handle_keycode(ehci_hid_usage_to_keycode(usage), 0));
        }
    }
    for (uint32_t i = 2; i < 8u; i++) {
        uint8_t usage = report[i];

        if (usage != 0u && !ehci_hid_report_contains_usage(kbd->last_report, usage)) {
            ehci_hid_queue_event(keyboard_handle_keycode(ehci_hid_usage_to_keycode(usage), 1));
        }
    }
    memcpy(kbd->last_report, report, 8u);
}

static int ehci_msc_command(struct ehci_msc_device *dev,
                            const uint8_t *cmd,
                            uint8_t cmd_len,
                            void *data,
                            uint32_t data_len,
                            uint8_t data_in) {
    uint32_t tag;
    uint32_t signature;
    uint32_t csw_tag;
    uint32_t cbw_token = 0u;
    uint32_t data_token = 0u;
    uint32_t csw_token;
    uint8_t csw_retry = 0u;

    if (dev == 0 || cmd == 0 || cmd_len == 0u || cmd_len > 16u || data_len > EHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_msc_phase = 0u;
    dev->last_msc_status = 0u;
    dev->last_msc_residue = 0u;
    tag = ++dev->tag;
    memset(dev->cbw, 0, 31u);
    ehci_write_u32le(dev->cbw + 0, MSC_CBW_SIGNATURE);
    ehci_write_u32le(dev->cbw + 4, tag);
    ehci_write_u32le(dev->cbw + 8, data_len);
    dev->cbw[12] = data_in ? 0x80u : 0x00u;
    dev->cbw[13] = dev->msc_lun;
    dev->cbw[14] = cmd_len;
    memcpy(dev->cbw + 15, cmd, cmd_len);
    dev->last_msc_phase = 1u;
    if (!ehci_bulk_transfer(dev, dev->bulk_out_ep, dev->bulk_out_mps,
                            &dev->bulk_out_toggle, dev->cbw_phys, 31u, 0u, &cbw_token, 0u)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("ehci: CBW send failed tag=%x op=%x out_toggle=%u cbw_token=%x\n",
               tag,
               (uint32_t)cmd[0],
               (uint32_t)dev->bulk_out_toggle,
               cbw_token);
        return 0;
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: CBW ok tag=%x op=%x out_toggle=%u in_toggle=%u\n",
               tag,
               (uint32_t)cmd[0],
               (uint32_t)dev->bulk_out_toggle,
               (uint32_t)dev->bulk_in_toggle);
    }
    ehci_delay_ms(EHCI_MSC_CBW_STAGE_SETTLE_MS);
    if (data_len != 0u) {
        if (!data_in && data != 0) {
            memcpy(dev->data, data, data_len);
        } else if (data_in) {
            memset(dev->data, 0, data_len);
        }
        dev->last_msc_phase = 2u;
        if (!ehci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_ep : dev->bulk_out_ep,
                                data_in ? dev->bulk_in_mps : dev->bulk_out_mps,
                                data_in ? &dev->bulk_in_toggle : &dev->bulk_out_toggle,
                                dev->data_phys,
                                data_len,
                                data_in,
                                &data_token,
                                EHCI_ASYNC_BULK_DATA_SPINS)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("ehci: DATA %s failed tag=%x op=%x len=%u toggle=%u data_token=%x in_mps=%u out_mps=%u\n",
                   data_in ? "in" : "out",
                   tag,
                   (uint32_t)cmd[0],
                   data_len,
                   (uint32_t)(data_in ? dev->bulk_in_toggle : dev->bulk_out_toggle),
                   data_token,
                   (uint32_t)dev->bulk_in_mps,
                   (uint32_t)dev->bulk_out_mps);
            return 0;
        }
        if (data_in && data != 0) {
            memcpy(data, dev->data, data_len);
        }
        ehci_delay_ms(EHCI_MSC_DATA_CSW_SETTLE_MS);
    }
    dev->last_msc_phase = 3u;
    for (;;) {
        memset(dev->csw, 0, 13u);
        csw_token = 0u;
        if (ehci_bulk_transfer(dev, dev->bulk_in_ep, dev->bulk_in_mps,
                               &dev->bulk_in_toggle, dev->csw_phys, 13u, 1u, &csw_token,
                               EHCI_ASYNC_BULK_CSW_SPINS)) {
            signature = ehci_read_u32le(dev->csw);
            csw_tag = ehci_read_u32le(dev->csw + 4);
            dev->last_msc_residue = ehci_read_u32le(dev->csw + 8);
            dev->last_msc_status = dev->csw[12];
            if (signature == MSC_CSW_SIGNATURE && csw_tag == tag) {
                if (dev->last_msc_status == 0u) {
                    break;
                }
                if (dev->last_msc_status == 1u) {
                    dev->last_msc_phase = 0u;
                    return 0;
                }
                if (dev->last_msc_status == 2u) {
                    dev->last_msc_phase = 4u;
                    return 0;
                }
                dev->last_msc_phase = 4u;
                return 0;
            }
            if (csw_retry != 0u) {
                dev->last_msc_phase = 4u;
                return 0;
            }
        } else {
            if (csw_retry != 0u) {
                dev->last_msc_phase = 3u;
                dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
                kprint("ehci: CSW recv failed x2 tag=%x op=%x in_toggle=%u csw_token=%x\n",
                       tag,
                       (uint32_t)cmd[0],
                       (uint32_t)dev->bulk_in_toggle,
                       csw_token);
                return 0;
            }
            if (EHCI_MSC_DEBUG) {
                kprint("ehci: CSW recv fail#1 tag=%x op=%x in_toggle=%u csw_token=%x\n",
                       tag,
                       (uint32_t)cmd[0],
                       (uint32_t)dev->bulk_in_toggle,
                       csw_token);
            }
            (void)ehci_clear_endpoint_halt(dev, dev->bulk_in_ep);
            dev->bulk_in_toggle = 0u;
            ehci_delay_ms(5u);
        }
        csw_retry = 1u;
    }
    dev->last_msc_phase = 0u;
    return 1;
}

static int ehci_clear_endpoint_halt(struct ehci_msc_device *dev, uint8_t ep) {
    struct usb_ctrl_request req;
    uint16_t mps = dev->control_mps != 0u ? dev->control_mps : 64u;

    req.type = 0x02u;
    req.request = USB_REQ_CLEAR_FEATURE;
    req.value = USB_FEATURE_ENDPOINT_HALT;
    req.index = ep;
    req.length = 0u;
    return ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
}

static int ehci_msc_reset_recovery(struct ehci_msc_device *dev) {
    struct usb_ctrl_request req;
    uint16_t mps;
    int ok;

    if (dev == 0) {
        return 0;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    req.type = 0x21u;
    req.request = USB_REQ_BULK_ONLY_RESET;
    req.value = 0u;
    req.index = dev->msc_interface_number;
    req.length = 0u;
    ok = ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
    ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
    ok = ehci_clear_endpoint_halt(dev, dev->bulk_in_ep) && ok;
    ehci_delay_ms(20u);
    ok = ehci_clear_endpoint_halt(dev, dev->bulk_out_ep) && ok;
    ehci_delay_ms(20u);
    dev->bulk_in_toggle = 0u;
    dev->bulk_out_toggle = 0u;
    dev->read_cache_valid = 0u;
    ehci_delay_ms(50u);
    return ok;
}

static struct ehci_msc_device *ehci_find_hub_by_addr(uint8_t address) {
    for (uint32_t i = 0; i < g_hub_count; i++) {
        if (g_hubs[i].present && g_hubs[i].address == address) {
            return &g_hubs[i];
        }
    }
    return 0;
}

static int ehci_reset_device_port(struct ehci_msc_device *dev) {
    struct ehci_msc_device *hub;
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint32_t root_status = 0u;

    if (dev == 0) {
        return 0;
    }
    if (dev->hub_addr == 0u) {
        if (!ehci_reset_port(dev->root_port, &root_status)) {
            return 0;
        }
        (void)root_status;
        dev->speed = EHCI_DEV_SPEED_HIGH;
        return 1;
    }
    hub = ehci_find_hub_by_addr(dev->hub_addr);
    if (hub == 0 || !ehci_hub_set_port_feature(hub, dev->hub_port, USB_HUB_FEATURE_PORT_RESET)) {
        return 0;
    }
    ehci_delay_ms(100u);
    for (uint32_t i = 0; i < 300u; i++) {
        if (!ehci_hub_get_port_status(hub, dev->hub_port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    ehci_hub_clear_changes(hub, dev->hub_port, change);
    if (!ehci_hub_get_port_status(hub, dev->hub_port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        return 0;
    }
    dev->speed = ehci_hub_child_speed(status);
    return 1;
}

static int ehci_msc_hard_reset_recovery(struct ehci_msc_device *dev) {
    uint16_t mps;

    if (dev == 0 || dev->address == 0u || dev->configuration == 0u) {
        return 0;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    if (!ehci_reset_device_port(dev)) {
        return 0;
    }
    ehci_delay_ms(80u);
    if (!ehci_set_address(dev, dev->address, mps)) {
        return 0;
    }
    ehci_delay_ms(20u);
    if (!ehci_set_configuration(dev, dev->configuration, mps)) {
        return 0;
    }
    dev->bulk_in_toggle = 0u;
    dev->bulk_out_toggle = 0u;
    dev->read_cache_valid = 0u;
    ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
    return 1;
}

static int ehci_msc_command_recover(struct ehci_msc_device *dev,
                                    const uint8_t *cmd,
                                    uint8_t cmd_len,
                                    void *data,
                                    uint32_t data_len,
                                    uint8_t data_in) {
    uint8_t failed_phase;
    uint8_t read10_csw_data_ok = 0u;

    if (ehci_msc_command(dev, cmd, cmd_len, data, data_len, data_in)) {
        return 1;
    }
    failed_phase = dev != 0 ? dev->last_msc_phase : 0u;
    if (dev != 0 && cmd != 0 && data != 0 &&
        failed_phase == 3u &&
        data_in && data_len != 0u && data_len <= EHCI_PAGE_SIZE &&
        cmd_len != 0u && cmd[0] == SCSI_READ_10 &&
        !ehci_msc_buffer_has_transport_signature((const uint8_t *)data)) {
        read10_csw_data_ok = 1u;
        if (data == dev->data && dev->read_cache != 0) {
            memcpy(dev->read_cache, data, data_len);
        }
    }
    if (dev != 0 && (failed_phase == 1u ||
                     failed_phase == 2u ||
                     failed_phase == 3u ||
                     failed_phase == 4u)) {
        int recovered = ehci_msc_reset_recovery(dev);

        if (!recovered) {
            recovered = ehci_msc_hard_reset_recovery(dev);
        }
        if (read10_csw_data_ok && recovered) {
            if (data == dev->data && dev->read_cache != 0) {
                memcpy(data, dev->read_cache, data_len);
            }
            dev->last_msc_phase = 0u;
            dev->last_msc_status = 0u;
            dev->last_msc_residue = 0u;
            return 1;
        }
    }
    return 0;
}

static uint8_t ehci_msc_get_max_lun(struct ehci_msc_device *dev) {
    struct usb_ctrl_request req;
    uint8_t lun = 0u;
    uint16_t mps;

    if (dev == 0) {
        return 0u;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    req.type = 0xa1u;
    req.request = USB_REQ_GET_MAX_LUN;
    req.value = 0u;
    req.index = dev->msc_interface_number;
    req.length = 1u;
    if (!ehci_control_transfer(dev, dev->address, mps, &req, &lun, 1u, 1u)) {
        return 0u;
    }
    if (lun > USB_MSC_MAX_LUN_LIMIT) {
        return 0u;
    }
    if (lun != 0u) {
        kprint("ehci: MSC GetMaxLUN reported %u\n", (uint32_t)lun);
    }
    return lun;
}

static void ehci_msc_record_sense(struct ehci_msc_device *dev, const uint8_t *sense, uint32_t length) {
    if (dev == 0 || sense == 0 || length < 14u) {
        return;
    }
    dev->last_sense_key = sense[2] & 0x0fu;
    dev->last_sense_asc = sense[12];
    dev->last_sense_ascq = sense[13];
}

static void ehci_msc_reduce_readahead(struct ehci_msc_device *dev) {
    if (dev == 0 || dev->read_ahead_sectors <= 1u) {
        return;
    }
    dev->read_ahead_sectors >>= 1;
    if (dev->read_ahead_sectors == 0u) {
        dev->read_ahead_sectors = 1u;
    }
    kprint("ehci: MSC readahead reduced to %u sectors\n",
           (uint32_t)dev->read_ahead_sectors);
}

static int ehci_msc_request_sense(struct ehci_msc_device *dev, uint8_t *sense, uint32_t length) {
    uint8_t cmd[6];
    int ok;

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = (uint8_t)length;
    ok = ehci_msc_command_recover(dev, cmd, 6u, sense, length, 1u);
    if (ok) {
        ehci_msc_record_sense(dev, sense, length);
    }
    return ok;
}

static int ehci_bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < size; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static int ehci_msc_buffer_has_transport_signature(const uint8_t *data) {
    uint32_t signature;

    if (data == 0) {
        return 0;
    }
    signature = ehci_read_u32le(data);
    return signature == MSC_CBW_SIGNATURE || signature == MSC_CSW_SIGNATURE;
}

static int ehci_msc_sync_cache(struct ehci_msc_device *dev, uint64_t lba, uint32_t count) {
    uint8_t cmd[10];

    if (dev == 0 || !dev->sync_cache_supported) {
        return 1;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_SYNCHRONIZE_CACHE_10;
    ehci_write_u32be(cmd + 2, (uint32_t)lba);
    cmd[7] = (uint8_t)((count >> 8) & 0xffu);
    cmd[8] = (uint8_t)(count & 0xffu);
    if (ehci_msc_command_recover(dev, cmd, 10u, 0, 0u, 0u)) {
        return 1;
    }
    (void)ehci_msc_request_sense(dev, dev->data, 18u);
    if (dev->last_sense_key == 0x05u) {
        dev->sync_cache_supported = 0u;
        kprint("ehci: MSC synchronize cache unsupported, continuing without it\n");
        return 1;
    }
    return 0;
}

static void ehci_msc_retry_delay(struct ehci_msc_device *dev) {
    if (dev == 0) {
        return;
    }
    if (dev->last_msc_phase == 0u && dev->last_msc_status != 0u) {
        (void)ehci_msc_request_sense(dev, dev->data, 18u);
        ehci_delay_ms(20u);
        return;
    }
    if (dev->last_msc_phase == 1u || dev->last_msc_phase == 2u ||
        dev->last_msc_phase == 3u || dev->last_msc_phase == 4u) {
        ehci_delay_ms(120u);
        return;
    }
    ehci_delay_ms(20u);
}

static int ehci_msc_test_unit_ready(struct ehci_msc_device *dev) {
    uint8_t cmd[6];

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return ehci_msc_command_recover(dev, cmd, 6u, 0, 0u, 0u);
}

static int ehci_msc_wait_ready(struct ehci_msc_device *dev) {
    uint8_t sense[18];
    uint8_t failed_phase;
    uint8_t failed_status;

    for (uint32_t i = 0; i < 20u; i++) {
        memset(sense, 0, sizeof(sense));
        if (ehci_msc_test_unit_ready(dev)) {
            return 1;
        }
        failed_phase = dev != 0 ? dev->last_msc_phase : 0u;
        failed_status = dev != 0 ? dev->last_msc_status : 0u;
        if (failed_phase == 1u ||
            failed_phase == 2u) {
            ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
            continue;
        }
        if (failed_phase == 3u) {
            if (failed_status == MSC_STATUS_TRANSPORT_ERROR) {
                ehci_delay_ms(50u);
                continue;
            }
            (void)ehci_msc_request_sense(dev, sense, sizeof(sense));
            if (dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au) {
                return 0;
            }
            ehci_delay_ms(100u);
            continue;
        }
        if (failed_phase == 4u) {
            ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
            continue;
        }
        (void)ehci_msc_request_sense(dev, sense, sizeof(sense));
        if (dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au) {
            return 0;
        }
        ehci_delay_ms(100u);
    }
    return 0;
}

static int ehci_msc_read_capacity(struct ehci_msc_device *dev) {
    uint8_t cmd[10];
    uint8_t cap[8];
    uint32_t last_lba;
    uint32_t block_len;

    memset(cmd, 0, sizeof(cmd));
    memset(cap, 0, sizeof(cap));
    cmd[0] = SCSI_READ_CAPACITY_10;
    if (!ehci_msc_command_recover(dev, cmd, 10u, cap, 8u, 1u)) {
        return 0;
    }
    last_lba = ehci_read_u32be(cap);
    block_len = ehci_read_u32be(cap + 4);
    if (block_len != EHCI_SECTOR_SIZE) {
        kprint("ehci: MSC read capacity block_len=%x last_lba=%x\n", block_len, last_lba);
        return 0;
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return dev->sector_count != 0u;
}

static int ehci_msc_probe(struct ehci_msc_device *dev) {
    uint8_t cmd[6];
    uint8_t inquiry[36];

    if (dev == 0) {
        return 0;
    }
    dev->max_lun = ehci_msc_get_max_lun(dev);
    kprint("ehci: MSC if=%u maxlun=%u in=%x/%u out=%x/%u ctrl_mps=%u\n",
           (uint32_t)dev->msc_interface_number,
           (uint32_t)dev->max_lun,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_in_mps,
           (uint32_t)dev->bulk_out_ep,
           (uint32_t)dev->bulk_out_mps,
           (uint32_t)dev->control_mps);
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: phys qh=%lx qtd=%lx data=%lx cbw=%lx csw=%lx\n",
               dev->qh_phys,
               dev->qtd_phys,
               dev->data_phys,
               dev->cbw_phys,
               dev->csw_phys);
    }
    for (uint8_t lun = 0u; lun <= dev->max_lun; lun++) {
        uint8_t inquiry_ok = 0u;

        dev->msc_lun = lun;
        dev->last_sense_key = 0u;
        dev->last_sense_asc = 0u;
        dev->last_sense_ascq = 0u;
        memset(cmd, 0, sizeof(cmd));
        memset(inquiry, 0, sizeof(inquiry));
        cmd[0] = SCSI_INQUIRY;
        cmd[4] = sizeof(inquiry);
        for (uint32_t i = 0; i < 5u; i++) {
            if (ehci_msc_command_recover(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
                inquiry_ok = 1u;
                break;
            }
            ehci_delay_ms(100u);
        }
        if (!inquiry_ok) {
            kprint("ehci: MSC lun%u inquiry failed phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        if (!ehci_msc_wait_ready(dev)) {
            kprint("ehci: MSC lun%u not ready sense=%x/%x/%x phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        for (uint32_t i = 0; i < 10u; i++) {
            if (ehci_msc_read_capacity(dev)) {
                return 1;
            }
            (void)ehci_msc_request_sense(dev, inquiry, 18u);
            ehci_delay_ms(100u);
        }
        kprint("ehci: MSC lun%u read capacity failed sense=%x/%x/%x phase=%u status=%u residue=%x\n",
               (uint32_t)lun,
               (uint32_t)dev->last_sense_key,
               (uint32_t)dev->last_sense_asc,
               (uint32_t)dev->last_sense_ascq,
               (uint32_t)dev->last_msc_phase,
               (uint32_t)dev->last_msc_status,
               dev->last_msc_residue);
    }
    return 0;
}

static int ehci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct ehci_msc_device *dev = (struct ehci_msc_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    uint8_t cmd[10];

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (count == 1u && dev->read_cache_valid &&
        lba >= dev->read_cache_lba &&
        lba < dev->read_cache_lba + dev->read_cache_count) {
        const uint8_t *cached = dev->read_cache + (uint32_t)(lba - dev->read_cache_lba) * EHCI_SECTOR_SIZE;

        if (ehci_msc_buffer_has_transport_signature(cached)) {
            kprint("ehci: MSC read cache lba=%lx contains transport signature, invalidating\n", lba);
            dev->read_cache_valid = 0u;
        } else {
            memcpy(out, cached, EHCI_SECTOR_SIZE);
            return 0;
        }
    }
    if (count == 1u && dev->read_cache != 0) {
        uint32_t readahead = dev->read_ahead_sectors;

        if (readahead > EHCI_MSC_READAHEAD_MAX_SECTORS) {
            readahead = EHCI_MSC_READAHEAD_MAX_SECTORS;
        }
        if ((uint64_t)readahead > dev->sector_count - lba) {
            readahead = (uint32_t)(dev->sector_count - lba);
        }
        if (readahead > 1u) {
            uint8_t cache_ok = 0u;

            for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                ehci_write_u32be(cmd + 2, (uint32_t)lba);
                cmd[7] = (uint8_t)((readahead >> 8) & 0xffu);
                cmd[8] = (uint8_t)(readahead & 0xffu);
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->read_cache,
                                             readahead * EHCI_SECTOR_SIZE,
                                             1u)) {
                    if (ehci_msc_buffer_has_transport_signature(dev->read_cache)) {
                        kprint("ehci: MSC read lba=%lx returned transport signature in data buffer\n", lba);
                        dev->read_cache_valid = 0u;
                        (void)ehci_msc_reset_recovery(dev);
                        ehci_msc_retry_delay(dev);
                        continue;
                    }
                    cache_ok = 1u;
                    break;
                }
                if (dev->last_msc_phase == 2u) {
                    ehci_msc_reduce_readahead(dev);
                    break;
                }
                ehci_msc_retry_delay(dev);
            }
            if (cache_ok) {
                dev->read_cache_lba = lba;
                dev->read_cache_count = (uint8_t)readahead;
                dev->read_cache_valid = 1u;
                memcpy(out, dev->read_cache, EHCI_SECTOR_SIZE);
                return 0;
            }
            dev->read_cache_valid = 0u;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ok = 0u;
        uint8_t verify_failed = 0u;

        for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = SCSI_READ_10;
            ehci_write_u32be(cmd + 2, (uint32_t)(lba + i));
            cmd[7] = 0u;
            cmd[8] = 1u;
            if (ehci_msc_command_recover(dev, cmd, 10u, out + i * EHCI_SECTOR_SIZE, EHCI_SECTOR_SIZE, 1u)) {
                if (ehci_msc_buffer_has_transport_signature(out + i * EHCI_SECTOR_SIZE)) {
                    kprint("ehci: MSC read lba=%lx returned transport signature in data buffer\n", lba + i);
                    verify_failed = 1u;
                    (void)ehci_msc_reset_recovery(dev);
                    ehci_msc_retry_delay(dev);
                    continue;
                }
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                ehci_write_u32be(cmd + 2, (uint32_t)(lba + i));
                cmd[7] = 0u;
                cmd[8] = 1u;
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->data,
                                             EHCI_SECTOR_SIZE,
                                             1u) &&
                    !ehci_msc_buffer_has_transport_signature(dev->data) &&
                    ehci_bytes_equal(out + i * EHCI_SECTOR_SIZE, dev->data, EHCI_SECTOR_SIZE)) {
                    ok = 1u;
                    break;
                }
                verify_failed = 1u;
            }
            ehci_msc_retry_delay(dev);
        }
        if (!ok) {
            uint8_t failed_phase = dev->last_msc_phase;
            uint8_t failed_status = dev->last_msc_status;

            (void)ehci_msc_request_sense(dev, dev->data, 18u);
            kprint("ehci: MSC read lba=%lx failed phase=%u status=%u verify=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)verify_failed,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            return -1;
        }
        if (count == 1u && dev->read_cache != 0) {
            memcpy(dev->read_cache, out, EHCI_SECTOR_SIZE);
            dev->read_cache_lba = lba;
            dev->read_cache_count = 1u;
            dev->read_cache_valid = 1u;
        }
    }
    return 0;
}

static int ehci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct ehci_msc_device *dev = (struct ehci_msc_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    uint8_t cmd[10];

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    dev->read_cache_valid = 0u;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ok = 0u;
        uint8_t verify_failed = 0u;

        for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = SCSI_WRITE_10;
            ehci_write_u32be(cmd + 2, (uint32_t)(lba + i));
            cmd[7] = 0u;
            cmd[8] = 1u;
            if (ehci_msc_command_recover(dev, cmd, 10u, (void *)(in + i * EHCI_SECTOR_SIZE), EHCI_SECTOR_SIZE, 0u)) {
                if (!ehci_msc_sync_cache(dev, lba + i, 1u)) {
                    ehci_msc_retry_delay(dev);
                    continue;
                }
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                ehci_write_u32be(cmd + 2, (uint32_t)(lba + i));
                cmd[7] = 0u;
                cmd[8] = 1u;
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->data,
                                             EHCI_SECTOR_SIZE,
                                             1u) &&
                    !ehci_msc_buffer_has_transport_signature(dev->data) &&
                    ehci_bytes_equal(dev->data, in + i * EHCI_SECTOR_SIZE, EHCI_SECTOR_SIZE)) {
                    ok = 1u;
                    break;
                }
                if (ehci_msc_buffer_has_transport_signature(dev->data)) {
                    kprint("ehci: MSC write verify lba=%lx returned transport signature in data buffer\n", lba + i);
                    (void)ehci_msc_reset_recovery(dev);
                }
                verify_failed = 1u;
            }
            ehci_msc_retry_delay(dev);
        }
        if (!ok) {
            uint8_t failed_phase = dev->last_msc_phase;
            uint8_t failed_status = dev->last_msc_status;

            (void)ehci_msc_request_sense(dev, dev->data, 18u);
            kprint("ehci: MSC write lba=%lx failed phase=%u status=%u verify=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)verify_failed,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            return -1;
        }
    }
    return 0;
}

static int ehci_config_has_hub_interface(const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;

    if (cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u && cfg[offset + 5u] == USB_CLASS_HUB) {
            return 1;
        }
        offset += len;
    }
    return 0;
}

static struct ehci_msc_device *ehci_select_probe_xfer(void) {
    if (g_msc_count < EHCI_MAX_MSC) {
        return &g_msc[g_msc_count];
    }
    if (g_hid_keyboard_count < EHCI_MAX_HID_KEYBOARDS) {
        return &g_hid_keyboards[g_hid_keyboard_count].xfer;
    }
    return 0;
}

static void ehci_log_device_summary(struct ehci_msc_device *dev,
                                    uint32_t root_port,
                                    const uint8_t *dev_desc,
                                    uint16_t mps0) {
    if (dev->hub_addr != 0u) {
        kprint("ehci: hub%u port%u dev addr=%u speed=%u cls=%x sub=%x proto=%x vid=%x pid=%x mps0=%u\n",
               (uint32_t)dev->hub_addr,
               (uint32_t)dev->hub_port,
               (uint32_t)dev->address,
               (uint32_t)dev->speed,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6],
               (uint32_t)ehci_read_u16le(dev_desc + 8),
               (uint32_t)ehci_read_u16le(dev_desc + 10),
               (uint32_t)mps0);
    } else {
        kprint("ehci: port%u dev addr=%u speed=%u cls=%x sub=%x proto=%x vid=%x pid=%x mps0=%u\n",
               root_port,
               (uint32_t)dev->address,
               (uint32_t)dev->speed,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6],
               (uint32_t)ehci_read_u16le(dev_desc + 8),
               (uint32_t)ehci_read_u16le(dev_desc + 10),
               (uint32_t)mps0);
    }
}

static void ehci_log_unsupported(struct ehci_msc_device *dev,
                                 uint32_t root_port,
                                 uint16_t total_len,
                                 const uint8_t *dev_desc) {
    if (dev->hub_addr != 0u) {
        kprint("ehci: hub%u port%u unsupported config total=%u devcls=%x devsub=%x devproto=%x\n",
               (uint32_t)dev->hub_addr,
               (uint32_t)dev->hub_port,
               (uint32_t)total_len,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6]);
    } else {
        kprint("ehci: port%u unsupported config total=%u devcls=%x devsub=%x devproto=%x\n",
               root_port,
               (uint32_t)total_len,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6]);
    }
}

static int ehci_hub_get_descriptor(struct ehci_msc_device *hub, uint8_t *buffer, uint16_t length) {
    struct usb_ctrl_request req;

    req.type = 0xa0u;
    req.request = USB_REQ_GET_DESCRIPTOR;
    req.value = (uint16_t)(USB_DESC_HUB << 8);
    req.index = 0u;
    req.length = length;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, buffer, length, 1u);
}

static int ehci_hub_set_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature) {
    struct usb_ctrl_request req;

    req.type = 0x23u;
    req.request = USB_REQ_SET_FEATURE;
    req.value = feature;
    req.index = port;
    req.length = 0u;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, 0, 0u, 0u);
}

static int ehci_hub_clear_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature) {
    struct usb_ctrl_request req;

    req.type = 0x23u;
    req.request = USB_REQ_CLEAR_FEATURE;
    req.value = feature;
    req.index = port;
    req.length = 0u;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, 0, 0u, 0u);
}

static int ehci_hub_get_port_status(struct ehci_msc_device *hub,
                                    uint8_t port,
                                    uint16_t *status_out,
                                    uint16_t *change_out) {
    struct usb_ctrl_request req;
    uint8_t data[4];

    req.type = 0xa3u;
    req.request = USB_REQ_GET_STATUS;
    req.value = 0u;
    req.index = port;
    req.length = sizeof(data);
    memset(data, 0, sizeof(data));
    if (!ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, data, sizeof(data), 1u)) {
        return 0;
    }
    if (status_out != 0) {
        *status_out = ehci_read_u16le(data);
    }
    if (change_out != 0) {
        *change_out = ehci_read_u16le(data + 2);
    }
    return 1;
}

static uint8_t ehci_hub_child_speed(uint16_t status) {
    if ((status & USB_HUB_PORT_LOW_SPEED) != 0u) {
        return EHCI_DEV_SPEED_LOW;
    }
    if ((status & USB_HUB_PORT_HIGH_SPEED) != 0u) {
        return EHCI_DEV_SPEED_HIGH;
    }
    return EHCI_DEV_SPEED_FULL;
}

static void ehci_hub_clear_changes(struct ehci_msc_device *hub, uint8_t port, uint16_t change) {
    if ((change & USB_HUB_PORT_CONNECTION) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION);
    }
    if ((change & USB_HUB_PORT_ENABLE) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_ENABLE);
    }
    if ((change & USB_HUB_PORT_RESET) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_RESET);
    }
}

static int ehci_enumerate_hub_port(struct ehci_msc_device *hub, uint32_t root_port, uint8_t port) {
    struct ehci_msc_device *dev;
    uint8_t dev_desc[18];
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint16_t mps0;

    if (!ehci_hub_get_port_status(hub, port, &status, &change)) {
        kprint("ehci: hub%u port%u status failed\n", (uint32_t)hub->address, (uint32_t)port);
        return 0;
    }
    ehci_hub_clear_changes(hub, port, change);
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        return 0;
    }
    kprint("ehci: hub%u port%u connected status=%x change=%x\n",
           (uint32_t)hub->address, (uint32_t)port, (uint32_t)status, (uint32_t)change);
    if (!ehci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
        kprint("ehci: hub%u port%u reset failed\n", (uint32_t)hub->address, (uint32_t)port);
        return 0;
    }
    ehci_delay_ms(80u);
    for (uint32_t i = 0; i < 200u; i++) {
        if (!ehci_hub_get_port_status(hub, port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    ehci_hub_clear_changes(hub, port, change);
    if (!ehci_hub_get_port_status(hub, port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("ehci: hub%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->address, (uint32_t)port, (uint32_t)status, (uint32_t)change);
        return 0;
    }

    dev = ehci_select_probe_xfer();
    if (dev == 0) {
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->controller = hub->controller;
    ehci_write_name(dev->name, g_msc_count);
    dev->speed = ehci_hub_child_speed(status);
    dev->root_port = (uint8_t)root_port;
    dev->hub_addr = hub->address;
    dev->hub_port = port;
    if (!ehci_alloc_msc_memory(dev)) {
        return 0;
    }
    mps0 = dev->speed == EHCI_DEV_SPEED_HIGH ? 64u : 8u;
    memset(dev_desc, 0, sizeof(dev_desc));
    for (uint32_t attempt = 0; attempt < 3u; attempt++) {
        ehci_delay_ms(20u);
        if (ehci_get_descriptor(dev, 0u, mps0, USB_DESC_DEVICE, 0u, dev_desc, 8u)) {
            mps0 = dev_desc[7] != 0u ? dev_desc[7] : mps0;
            return ehci_finish_device_enumeration(dev, root_port, dev_desc, mps0);
        }
    }
    kprint("ehci: hub%u port%u get device descriptor failed speed=%u status=%x\n",
           (uint32_t)hub->address, (uint32_t)port, (uint32_t)dev->speed, (uint32_t)status);
    return 0;
}

static int ehci_probe_hub(struct ehci_msc_device *dev,
                          uint32_t root_port,
                          uint16_t mps0,
                          const uint8_t *cfg,
                          uint32_t cfg_len) {
    struct ehci_msc_device *hub;
    uint8_t hub_desc[32];
    uint8_t port_count;
    uint32_t pwr_ms;
    uint32_t hub_index;

    (void)cfg_len;
    if (g_hub_count >= EHCI_MAX_HUBS) {
        kprint("ehci: hub limit reached\n");
        return 0;
    }
    dev->configuration = cfg[5];
    dev->control_mps = mps0;
    dev->bulk_in_mps = mps0;
    if (!ehci_set_configuration(dev, dev->configuration, mps0)) {
        kprint("ehci: port%u hub set config failed\n", root_port);
        return 0;
    }
    hub_index = g_hub_count++;
    hub = &g_hubs[hub_index];
    memset(hub, 0, sizeof(*hub));
    *hub = *dev;
    hub->present = 1u;
    memset(hub_desc, 0, sizeof(hub_desc));
    if (!ehci_hub_get_descriptor(hub, hub_desc, 8u)) {
        kprint("ehci: hub%u descriptor failed\n", (uint32_t)hub->address);
        return 0;
    }
    port_count = hub_desc[2];
    if (port_count > 8u) {
        port_count = 8u;
    }
    pwr_ms = (uint32_t)hub_desc[5] * 2u;
    if (pwr_ms < 100u) {
        pwr_ms = 100u;
    }
    kprint("ehci: hub%u rootport=%u ports=%u pwr=%u\n",
           (uint32_t)hub->address,
           root_port,
           (uint32_t)port_count,
           pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)ehci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_POWER);
    }
    ehci_delay_ms(pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)ehci_enumerate_hub_port(hub, root_port, port);
    }
    return 1;
}

static int ehci_finish_device_enumeration(struct ehci_msc_device *dev,
                                          uint32_t root_port,
                                          uint8_t *dev_desc,
                                          uint16_t mps0) {
    struct ehci_hid_keyboard *kbd;
    struct ehci_msc_device saved;
    uint8_t cfg[256];
    uint16_t total_len;
    uint8_t hid_parsed;

    dev->control_mps = mps0;
    dev->bulk_in_mps = mps0;
    dev->root_port = (uint8_t)root_port;
    dev->address = g_next_address++;
    if (!ehci_set_address(dev, dev->address, mps0)) {
        return 0;
    }
    ehci_delay_ms(10u);
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_DEVICE, 0u, dev_desc, 18u)) {
        return 0;
    }
    ehci_log_device_summary(dev, root_port, dev_desc, mps0);
    memset(cfg, 0, sizeof(cfg));
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_CONFIGURATION, 0u, cfg, 9u)) {
        return 0;
    }
    total_len = ehci_read_u16le(cfg + 2);
    if (total_len > sizeof(cfg)) {
        total_len = sizeof(cfg);
    }
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_CONFIGURATION, 0u, cfg, total_len)) {
        return 0;
    }
    if (dev_desc[4] == USB_CLASS_HUB || ehci_config_has_hub_interface(cfg, total_len)) {
        return ehci_probe_hub(dev, root_port, mps0, cfg, total_len);
    }
    if (g_msc_count < EHCI_MAX_MSC && ehci_parse_msc_config(dev, cfg, total_len)) {
        if (EHCI_MSC_DEBUG) {
            ehci_log_config_descriptor(cfg, total_len);
        }
        if (!ehci_set_configuration(dev, dev->configuration, mps0)) {
            return 0;
        }
        ehci_delay_ms(EHCI_SET_CONFIGURATION_SETTLE_MS);
        dev->bulk_in_toggle = 0u;
        dev->bulk_out_toggle = 0u;
        dev->tag = 0x12340000u;
        if (!ehci_msc_probe(dev)) {
            kprint("ehci: port%u MSC probe failed\n", root_port);
            return 0;
        }
        dev->blockdev.name = dev->name;
        dev->blockdev.block_size = EHCI_SECTOR_SIZE;
        dev->blockdev.block_count = dev->sector_count;
        dev->blockdev.read = ehci_msc_read_impl;
        dev->blockdev.write = ehci_msc_write_impl;
        dev->blockdev.driver_data = dev;
        dev->present = 1u;
        if (blockdev_register(&dev->blockdev) != 0) {
            dev->present = 0u;
            return 0;
        }
        kprint("ehci: port%u %s lun=%u sectors=%lx in=%x out=%x\n",
               root_port, dev->name, (uint32_t)dev->msc_lun, dev->sector_count,
               (uint32_t)dev->bulk_in_ep, (uint32_t)dev->bulk_out_ep);
        g_msc_count++;
        return 1;
    }
    if (g_hid_keyboard_count >= EHCI_MAX_HID_KEYBOARDS) {
        return 0;
    }
    kbd = &g_hid_keyboards[g_hid_keyboard_count];
    saved = *dev;
    memset(kbd, 0, sizeof(*kbd));
    kbd->xfer = saved;
    kbd->address = saved.address;
    kbd->xfer.bulk_in_mps = mps0;
    hid_parsed = (uint8_t)ehci_parse_hid_keyboard_config(kbd, cfg, total_len);
    if (!hid_parsed) {
        ehci_log_unsupported(dev, root_port, total_len, dev_desc);
        return 0;
    }
    if (!ehci_set_configuration(&kbd->xfer, kbd->configuration, mps0)) {
        kprint("ehci: port%u hid set config failed\n", root_port);
        return 0;
    }
    if (!ehci_hid_set_protocol(kbd, 0u)) {
        kprint("ehci: port%u hid set protocol failed\n", root_port);
    }
    if (!ehci_hid_set_idle(kbd)) {
        kprint("ehci: port%u hid set idle failed\n", root_port);
    }
    memset(kbd->last_report, 0, sizeof(kbd->last_report));
    kbd->report_fail_logged = 0u;
    kbd->present = 1u;
    if (kbd->xfer.hub_addr != 0u) {
        kprint("ehci: hub%u port%u hidkbd%u addr=%u iface=%u in=%x mps=%u\n",
               (uint32_t)kbd->xfer.hub_addr,
               (uint32_t)kbd->xfer.hub_port,
               g_hid_keyboard_count,
               (uint32_t)kbd->address,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interrupt_in_ep,
               (uint32_t)kbd->interrupt_in_mps);
    } else {
        kprint("ehci: port%u hidkbd%u addr=%u iface=%u in=%x mps=%u\n",
               root_port,
               g_hid_keyboard_count,
               (uint32_t)kbd->address,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interrupt_in_ep,
               (uint32_t)kbd->interrupt_in_mps);
    }
    g_hid_keyboard_count++;
    return 1;
}

static int ehci_enumerate_port(uint32_t port_index) {
    struct ehci_msc_device *dev;
    uint8_t dev_desc[18];
    uint16_t mps0 = 64u;
    uint32_t port_off = 0x44u + port_index * 4u;
    uint32_t port = ehci_read_op(port_off);

    if ((g_msc_count >= EHCI_MAX_MSC && g_hid_keyboard_count >= EHCI_MAX_HID_KEYBOARDS) ||
        (port & EHCI_PORT_CONNECT) == 0u) {
        return 0;
    }
    if (!ehci_reset_port(port_index, &port)) {
        if ((port & EHCI_PORT_OWNER) != 0u) {
            kprint("ehci: port%u handed to companion portsc=%x\n", port_index, port);
        } else {
            kprint("ehci: port%u not high-speed portsc=%x\n", port_index, port);
        }
        return 0;
    }

    dev = ehci_select_probe_xfer();
    if (dev == 0) {
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->controller = g_ehci;
    ehci_write_name(dev->name, g_msc_count);
    dev->speed = EHCI_DEV_SPEED_HIGH;
    if (!ehci_alloc_msc_memory(dev)) {
        return 0;
    }
    if (!ehci_get_initial_device_descriptor(dev, port_index, dev_desc, 8u, mps0, &port)) {
        kprint("ehci: port%u get device descriptor failed\n", port_index);
        return 0;
    }
    mps0 = dev_desc[7] != 0u ? dev_desc[7] : 64u;
    return ehci_finish_device_enumeration(dev, port_index, dev_desc, mps0);
}

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

    g_msc_count = 0u;
    g_hub_count = 0u;
    g_hid_keyboard_count = 0u;
    g_next_address = 1u;
    g_hid_event_head = 0u;
    g_hid_event_tail = 0u;
    g_hid_event_count = 0u;
    g_hid_poll_divider = 0u;
    g_async_head_phys = 0u;
    g_async_dummy_qtd_phys = 0u;
    g_periodic_list_phys = 0u;
    g_async_head = 0;
    g_async_dummy_qtd = 0;
    g_periodic_list = 0;
    memset(g_msc, 0, sizeof(g_msc));
    memset(g_hubs, 0, sizeof(g_hubs));
    memset(g_hid_keyboards, 0, sizeof(g_hid_keyboards));
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
        g_ehci.cap = (volatile uint8_t *)hal_phys_direct_map(mmio);
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
    return g_msc_count;
}

uint32_t ehci_hid_keyboard_count(void) {
    return g_hid_keyboard_count;
}

int ehci_poll_keyboard_event(struct keyboard_event *out) {
    uint8_t report[8];

    if (out == 0) {
        return 0;
    }
    if (ehci_hid_pop_event(out)) {
        return 1;
    }
    if (g_hid_keyboard_count == 0u) {
        return 0;
    }
    g_hid_poll_divider++;
    if ((g_hid_poll_divider & 3u) != 0u) {
        return 0;
    }
    for (uint32_t i = 0; i < g_hid_keyboard_count; i++) {
        struct ehci_hid_keyboard *kbd = &g_hid_keyboards[i];

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
