#include "drivers/usb/xhci.h"

#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/input/keyboard.h"
#include "drivers/usb/usb_bytes.h"
#include "drivers/usb/usb_hid_keymap.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

enum {
    XHCI_PAGE_SIZE = 4096u,
    XHCI_SECTOR_SIZE = 512u,
    XHCI_TRB_SIZE = 16u,
    XHCI_RING_TRBS = 64u,
    XHCI_ERST_ENTRIES = 1u,
    XHCI_MAX_SCRATCHPADS = 32u,
    XHCI_MAX_CONTROLLERS = 4u,
    XHCI_MAX_ENUM_DEVICES = 32u,
    XHCI_MAX_ROOT_PORTS = 256u,
    XHCI_MAX_HUB_PORTS = 15u,
    XHCI_MAX_HID_KEYBOARDS = 4u,
    XHCI_HID_EVENT_QUEUE_SIZE = 64u,
    XHCI_HID_REPORT_QUEUE_SIZE = 16u,
    XHCI_HID_REPORT_BYTES = 64u,
    XHCI_HID_REPEAT_DELAY_TICKS = 35u,
    XHCI_HID_REPEAT_RATE_TICKS = 5u,
    XHCI_HOTPLUG_SCAN_TICKS = 25u,
    XHCI_MMIO_MAP_SIZE = 0x100000u,
    XHCI_WAIT_SPINS_DEFAULT = 50000000u,
    XHCI_MSC_BULK_WAIT_SPINS = 250000000u,
    XHCI_MSC_CONFIG_SETTLE_MS = 150u,
    XHCI_MSC_RESET_SETTLE_MS = 250u,
    XHCI_MSC_RW_RETRIES = 4u,
    XHCI_HID_REPORT_WAIT_SPINS = 50000u,
    XHCI_HUB_STATUS_WAIT_SPINS = 2000000u,
    XHCI_HID_MAX_POLL_FAILURES = 3u,
    XHCI_ADDRESS_SETTLE_MS = 20u,
    XHCI_DESCRIPTOR_RETRIES = 1u,

    XHCI_USBCMD_RS = 1u << 0,
    XHCI_USBCMD_HCRST = 1u << 1,
    XHCI_USBCMD_INTE = 1u << 2,
    XHCI_USBSTS_HCH = 1u << 0,
    XHCI_USBSTS_CNR = 1u << 11,

    XHCI_OP_USBCMD = 0x00u,
    XHCI_OP_USBSTS = 0x04u,
    XHCI_OP_PAGESIZE = 0x08u,
    XHCI_OP_DNCTRL = 0x14u,
    XHCI_OP_CRCR = 0x18u,
    XHCI_OP_DCBAAP = 0x30u,
    XHCI_OP_CONFIG = 0x38u,
    XHCI_OP_PORT_BASE = 0x400u,
    XHCI_PORT_REG_STRIDE = 0x10u,

    XHCI_INTR_IMAN = 0x00u,
    XHCI_INTR_IMOD = 0x04u,
    XHCI_INTR_ERSTSZ = 0x08u,
    XHCI_INTR_ERSTBA = 0x10u,
    XHCI_INTR_ERDP = 0x18u,

    XHCI_PORTSC_CCS = 1u << 0,
    XHCI_PORTSC_PED = 1u << 1,
    XHCI_PORTSC_PR = 1u << 4,
    XHCI_PORTSC_PP = 1u << 9,
    XHCI_PORTSC_RW_PRESERVE = 0x0e00c3e0u,
    XHCI_PORTSC_CHANGE_BITS = 0x00fe0000u,

    XHCI_EXT_CAP_LEGACY_SUPPORT = 1u,
    XHCI_EXT_CAP_SUPPORTED_PROTOCOL = 2u,
    XHCI_LEGACY_BIOS_OWNED = 1u << 16,
    XHCI_LEGACY_OS_OWNED = 1u << 24
};

enum {
    XHCI_TRB_LINK = 6u,
    XHCI_TRB_SETUP_STAGE = 2u,
    XHCI_TRB_DATA_STAGE = 3u,
    XHCI_TRB_STATUS_STAGE = 4u,
    XHCI_TRB_NORMAL = 1u,
    XHCI_TRB_ENABLE_SLOT = 9u,
    XHCI_TRB_ADDRESS_DEVICE = 11u,
    XHCI_TRB_CONFIGURE_ENDPOINT = 12u,
    XHCI_TRB_EVALUATE_CONTEXT = 13u,
    XHCI_TRB_RESET_ENDPOINT = 14u,
    XHCI_TRB_STOP_ENDPOINT = 15u,
    XHCI_TRB_SET_TR_DEQUEUE = 16u,
    XHCI_TRB_TRANSFER_EVENT = 32u,
    XHCI_TRB_COMMAND_COMPLETION = 33u,

    XHCI_CC_SUCCESS = 1u,
    XHCI_CC_STALL_ERROR = 6u,
    XHCI_CC_SHORT_PACKET = 13u,
    XHCI_CC_CONTEXT_STATE_ERROR = 19u,

    XHCI_EP_STATE_DISABLED = 0u,
    XHCI_EP_STATE_RUNNING = 1u,
    XHCI_EP_STATE_HALTED = 2u,
    XHCI_EP_STATE_STOPPED = 3u,
    XHCI_EP_STATE_ERROR = 4u,

    XHCI_SPEED_FULL = 1u,
    XHCI_SPEED_LOW = 2u,
    XHCI_SPEED_HIGH = 3u,
    XHCI_SPEED_SUPER = 4u,

    XHCI_SLOT_FLAG = 1u << 0,
    XHCI_EP0_FLAG = 1u << 1,
    XHCI_SLOT_HUB = 1u << 26,
    XHCI_SLOT_LAST_CTX_1 = 1u << 27,
    XHCI_EP_CONTEXT_CERR_3 = 3u << 1,
    XHCI_ERDP_EHB = 1u << 3,

    USB_REQ_GET_STATUS = 0x00u,
    USB_REQ_CLEAR_FEATURE = 0x01u,
    USB_REQ_SET_FEATURE = 0x03u,
    USB_REQ_GET_MAX_LUN = 0xfeu,
    USB_REQ_BULK_ONLY_RESET = 0xffu,
    USB_REQ_GET_DESCRIPTOR = 0x06u,
    USB_REQ_SET_CONFIGURATION = 0x09u,
    USB_REQ_GET_REPORT = 0x01u,
    USB_REQ_SET_REPORT = 0x09u,
    USB_REQ_SET_IDLE = 0x0au,
    USB_REQ_SET_PROTOCOL = 0x0bu,
    USB_DESC_DEVICE = 0x01u,
    USB_DESC_CONFIGURATION = 0x02u,
    USB_DESC_HUB = 0x29u,
    USB_DESC_SS_HUB = 0x2au,
    USB_CLASS_HID = 0x03u,
    USB_CLASS_HUB = 0x09u,
    USB_CLASS_MASS_STORAGE = 0x08u,
    USB_SUBCLASS_BOOT = 0x01u,
    USB_SUBCLASS_SCSI = 0x06u,
    USB_MSC_MAX_LUN_LIMIT = 15u,
    USB_PROTO_KEYBOARD = 0x01u,
    USB_PROTO_BULK_ONLY = 0x50u,

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
    USB_FEATURE_ENDPOINT_HALT = 0u,

    MSC_CBW_SIGNATURE = 0x43425355u,
    MSC_CSW_SIGNATURE = 0x53425355u,
    MSC_STATUS_TRANSPORT_ERROR = 0xffu,
    SCSI_TEST_UNIT_READY = 0x00u,
    SCSI_REQUEST_SENSE = 0x03u,
    SCSI_INQUIRY = 0x12u,
    SCSI_READ_CAPACITY_10 = 0x25u,
    SCSI_READ_10 = 0x28u,
    SCSI_WRITE_10 = 0x2au
};

struct xhci_trb {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
};

struct xhci_erst_entry {
    uint32_t ring_base_lo;
    uint32_t ring_base_hi;
    uint32_t ring_size;
    uint32_t rsvd;
};

struct xhci_state {
    volatile uint8_t *cap;
    volatile uint8_t *op;
    volatile uint8_t *runtime;
    volatile uint8_t *doorbell;
    uint8_t cap_length;
    uint8_t max_slots;
    uint16_t max_intrs;
    uint8_t max_ports;
    uint32_t page_size;
    uint32_t connected_ports;
    uint64_t dcbaa_phys;
    uint64_t scratchpad_array_phys;
    uint64_t command_ring_phys;
    uint64_t event_ring_phys;
    uint64_t erst_phys;
    uint64_t *dcbaa;
    uint64_t *scratchpad_array;
    struct xhci_trb *command_ring;
    struct xhci_trb *event_ring;
    struct xhci_erst_entry *erst;
    uint32_t command_enqueue;
    uint8_t command_cycle;
    uint32_t event_dequeue;
    uint8_t event_cycle;
    uint8_t context_size;
    uint8_t initialized;
    uint8_t root_port_slots[XHCI_MAX_ROOT_PORTS];
};

struct xhci_enum_device {
    char name[12];
    uint8_t used;
    uint8_t controller_index;
    uint8_t slot_id;
    uint8_t port;
    uint8_t speed;
    uint8_t route_depth;
    uint8_t parent_slot_id;
    uint8_t parent_port;
    uint8_t hub_port_count;
    uint8_t configuration;
    uint8_t msc_interface_number;
    uint8_t msc_lun;
    uint8_t max_lun;
    uint8_t last_msc_phase;
    uint8_t last_msc_status;
    uint8_t last_bulk_completion;
    uint8_t last_sense_key;
    uint8_t last_sense_asc;
    uint8_t last_sense_ascq;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint8_t bulk_in_epid;
    uint8_t bulk_out_epid;
    uint16_t bulk_in_mps;
    uint16_t bulk_out_mps;
    uint32_t route_string;
    uint32_t bulk_in_enqueue;
    uint32_t bulk_out_enqueue;
    uint8_t bulk_in_cycle;
    uint8_t bulk_out_cycle;
    uint32_t tag;
    uint32_t last_msc_residue;
    uint64_t sector_count;
    uint64_t input_context_phys;
    uint64_t device_context_phys;
    uint64_t ep0_ring_phys;
    uint64_t bulk_in_ring_phys;
    uint64_t bulk_out_ring_phys;
    uint64_t descriptor_phys;
    uint64_t data_phys;
    uint64_t cbw_phys;
    uint64_t csw_phys;
    uint8_t *input_context;
    uint8_t *device_context;
    uint8_t *descriptor;
    uint8_t *data;
    uint8_t *cbw;
    uint8_t *csw;
    struct xhci_trb *ep0_ring;
    struct xhci_trb *bulk_in_ring;
    struct xhci_trb *bulk_out_ring;
    uint32_t ep0_enqueue;
    uint8_t ep0_cycle;
    struct block_device blockdev;
};

struct xhci_hid_keyboard {
    uint8_t present;
    uint8_t report_fail_logged;
    uint8_t poll_fail_count;
    uint8_t poll_disabled;
    uint8_t interface_number;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t boot_protocol;
    uint8_t interrupt_in_ep;
    uint8_t interrupt_in_epid;
    uint8_t interrupt_in_interval;
    uint8_t interrupt_pending;
    uint8_t report_size;
    uint8_t report_queue_head;
    uint8_t report_queue_tail;
    uint8_t report_queue_count;
    uint8_t led_state;
    uint8_t led_fail_logged;
    uint8_t repeat_usage;
    uint8_t repeat_active;
    uint32_t repeat_ticks;
    uint16_t interrupt_in_mps;
    uint64_t interrupt_in_ring_phys;
    uint64_t report_phys;
    struct xhci_trb *interrupt_in_ring;
    uint8_t *report;
    uint32_t interrupt_in_enqueue;
    uint8_t interrupt_in_cycle;
    uint8_t last_report[8];
    uint8_t report_queue_completion[XHCI_HID_REPORT_QUEUE_SIZE];
    uint8_t report_queue[XHCI_HID_REPORT_QUEUE_SIZE][8];
    struct xhci_enum_device *dev;
};

static struct xhci_state g_xhci;
static struct xhci_state g_xhci_controllers[XHCI_MAX_CONTROLLERS];
static uint32_t g_xhci_controller_count;
static uint8_t g_xhci_active_controller;
static struct xhci_enum_device g_enum_devices[XHCI_MAX_ENUM_DEVICES];
static uint32_t g_xhci_msc_count;
static struct xhci_hid_keyboard g_hid_keyboards[XHCI_MAX_HID_KEYBOARDS];
static uint32_t g_hid_keyboard_count;
static struct keyboard_event g_hid_event_queue[XHCI_HID_EVENT_QUEUE_SIZE];
static uint32_t g_hid_event_head;
static uint32_t g_hid_event_tail;
static uint32_t g_hid_event_count;
static uint32_t g_hid_last_repeat_tick;
static uint32_t g_xhci_last_hotplug_tick;
static volatile uint32_t g_xhci_busy;

static int xhci_hid_defer_transfer_event(uint8_t slot_id, uint8_t endpoint_id, uint32_t completion);
static void xhci_hid_release_all_keys(struct xhci_hid_keyboard *kbd);

static int xhci_try_begin_busy(void) {
    if (g_xhci_busy != 0u) {
        return 0;
    }
    g_xhci_busy = 1u;
    return 1;
}

static void xhci_end_busy(void) {
    g_xhci_busy = 0u;
}

static void xhci_save_active_controller(void) {
    if (g_xhci_active_controller < XHCI_MAX_CONTROLLERS) {
        g_xhci_controllers[g_xhci_active_controller] = g_xhci;
    }
}

static int xhci_select_controller(uint8_t index) {
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

static uint32_t xhci_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static void xhci_write32(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static void xhci_write64(volatile uint8_t *base, uint32_t offset, uint64_t value) {
    xhci_write32(base, offset, (uint32_t)value);
    xhci_write32(base, offset + 4u, (uint32_t)(value >> 32));
}

static int xhci_wait_clear(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((xhci_read32(base, offset) & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int xhci_wait_set(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((xhci_read32(base, offset) & mask) != 0u) {
            return 1;
        }
    }
    return 0;
}

static uint32_t xhci_max_scratchpads(uint32_t hcsparams2) {
    uint32_t hi = (hcsparams2 >> 21) & 0x1fu;
    uint32_t lo = (hcsparams2 >> 27) & 0x1fu;

    return (hi << 5) | lo;
}

static uint8_t *xhci_context_ptr(uint8_t *base, uint32_t index) {
    return base + index * g_xhci.context_size;
}

static void xhci_ring_trb_set(struct xhci_trb *trb,
                              uint64_t parameter,
                              uint32_t status,
                              uint32_t control) {
    trb->parameter_lo = (uint32_t)parameter;
    trb->parameter_hi = (uint32_t)(parameter >> 32);
    trb->status = status;
    trb->control = control;
}

static uint32_t xhci_trb_type(uint32_t control) {
    return (control >> 10) & 0x3fu;
}

static void xhci_delay_spin(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

static void xhci_delay_ms(uint32_t ms) {
    while (ms-- != 0u) {
        xhci_delay_spin(100000u);
    }
}

static void xhci_bios_handoff(uint32_t hccparams1) {
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

static void xhci_log_supported_protocols(uint32_t hccparams1) {
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
            kprint("xhci: protocol %s rev=%u.%u ports=%u..%u raw=%x\n",
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

static void xhci_power_root_ports(void) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports; port++) {
        uint32_t offset = XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
        uint32_t portsc = xhci_read32(g_xhci.op, offset);

        if ((portsc & XHCI_PORTSC_PP) == 0u) {
            xhci_write32(g_xhci.op, offset, (portsc & XHCI_PORTSC_RW_PRESERVE) | XHCI_PORTSC_PP);
        }
    }
    xhci_delay_ms(100u);
}

static void xhci_log_root_ports(uint8_t controller_index, const char *phase) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE);

        kprint("xhci%u: root port%u %s conn=%u enabled=%u powered=%u speed=%u portsc=%x\n",
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

static int xhci_alloc_page(uint64_t *phys_out, void **virt_out) {
    uint64_t phys;
    void *virt;

    if (phys_out == 0 || virt_out == 0) {
        return 0;
    }
    phys = pmm_alloc_page();
    if (phys == 0u || phys > 0xffffffffull) {
        return 0;
    }
    virt = hal_phys_direct_map(phys);
    if (virt == 0) {
        return 0;
    }
    memset(virt, 0, XHCI_PAGE_SIZE);
    *phys_out = phys;
    *virt_out = virt;
    return 1;
}

static int xhci_alloc_core_rings(uint32_t scratchpads) {
    if (!xhci_alloc_page(&g_xhci.dcbaa_phys, (void **)&g_xhci.dcbaa) ||
        !xhci_alloc_page(&g_xhci.command_ring_phys, (void **)&g_xhci.command_ring) ||
        !xhci_alloc_page(&g_xhci.event_ring_phys, (void **)&g_xhci.event_ring) ||
        !xhci_alloc_page(&g_xhci.erst_phys, (void **)&g_xhci.erst)) {
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
            scratchpads = XHCI_MAX_SCRATCHPADS;
        }
        if (!xhci_alloc_page(&g_xhci.scratchpad_array_phys, (void **)&g_xhci.scratchpad_array)) {
            return 0;
        }
        for (uint32_t i = 0; i < scratchpads; i++) {
            uint64_t scratch_phys;
            void *scratch_virt;

            if (!xhci_alloc_page(&scratch_phys, &scratch_virt)) {
                return 0;
            }
            (void)scratch_virt;
            g_xhci.scratchpad_array[i] = scratch_phys;
        }
        g_xhci.dcbaa[0] = g_xhci.scratchpad_array_phys;
    }
    return 1;
}

static int xhci_alloc_enum_device(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_page(&dev->input_context_phys, (void **)&dev->input_context) ||
        !xhci_alloc_page(&dev->device_context_phys, (void **)&dev->device_context) ||
        !xhci_alloc_page(&dev->ep0_ring_phys, (void **)&dev->ep0_ring) ||
        !xhci_alloc_page(&dev->descriptor_phys, (void **)&dev->descriptor)) {
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

static int xhci_alloc_msc_resources(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_page(&dev->bulk_in_ring_phys, (void **)&dev->bulk_in_ring) ||
        !xhci_alloc_page(&dev->bulk_out_ring_phys, (void **)&dev->bulk_out_ring) ||
        !xhci_alloc_page(&dev->data_phys, (void **)&dev->data) ||
        !xhci_alloc_page(&dev->cbw_phys, (void **)&dev->cbw) ||
        !xhci_alloc_page(&dev->csw_phys, (void **)&dev->csw)) {
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
    return 1;
}

static int xhci_alloc_hid_keyboard_resources(struct xhci_hid_keyboard *kbd) {
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

static struct xhci_enum_device *xhci_alloc_device_record(void) {
    for (uint32_t i = 0; i < XHCI_MAX_ENUM_DEVICES; i++) {
        if (!g_enum_devices[i].used && g_enum_devices[i].slot_id == 0u) {
            return &g_enum_devices[i];
        }
    }
    kprint("xhci: enum device table full max=%u\n", XHCI_MAX_ENUM_DEVICES);
    return 0;
}

static void xhci_ring_command(uint64_t parameter, uint32_t status, uint32_t control) {
    struct xhci_trb *trb = &g_xhci.command_ring[g_xhci.command_enqueue];

    xhci_ring_trb_set(trb, parameter, status, control | g_xhci.command_cycle);
    g_xhci.command_enqueue++;
    if (g_xhci.command_enqueue >= XHCI_RING_TRBS - 1u) {
        g_xhci.command_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (g_xhci.command_cycle != 0u ? 1u : 0u);
        g_xhci.command_cycle ^= 1u;
        g_xhci.command_enqueue = 0u;
    }
    xhci_write32(g_xhci.doorbell, 0u, 0u);
}

static int xhci_pop_event(struct xhci_trb *out) {
    struct xhci_trb *event = &g_xhci.event_ring[g_xhci.event_dequeue];
    uint32_t control = event->control;

    if (out == 0 || (control & 1u) != g_xhci.event_cycle) {
        return 0;
    }
    *out = *event;
    g_xhci.event_dequeue++;
    if (g_xhci.event_dequeue >= XHCI_RING_TRBS) {
        g_xhci.event_dequeue = 0u;
        g_xhci.event_cycle ^= 1u;
    }
    xhci_write64(g_xhci.runtime + 0x20u, XHCI_INTR_ERDP,
                 g_xhci.event_ring_phys + g_xhci.event_dequeue * XHCI_TRB_SIZE + XHCI_ERDP_EHB);
    return 1;
}

static int xhci_wait_command_completion(uint8_t *slot_id_out, uint32_t *completion_out) {
    for (uint32_t spin = 0; spin < XHCI_WAIT_SPINS_DEFAULT; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
        if (xhci_trb_type(control) == XHCI_TRB_TRANSFER_EVENT) {
            uint8_t event_slot_id = (uint8_t)((control >> 24) & 0xffu);
            uint8_t event_endpoint_id = (uint8_t)((control >> 16) & 0x1fu);
            uint32_t completion = (event.status >> 24) & 0xffu;

            (void)xhci_hid_defer_transfer_event(event_slot_id, event_endpoint_id, completion);
            continue;
        }
        if (xhci_trb_type(control) != XHCI_TRB_COMMAND_COMPLETION) {
            continue;
        }
        if (slot_id_out != 0) {
            *slot_id_out = (uint8_t)((control >> 24) & 0xffu);
        }
        if (completion_out != 0) {
            *completion_out = (event.status >> 24) & 0xffu;
        }
        return 1;
    }
    return 0;
}

static int xhci_wait_transfer_event_spins(uint8_t slot_id,
                                          uint8_t endpoint_id,
                                          uint32_t *completion_out,
                                          uint64_t expected_trb_phys,
                                          uint32_t max_spins) {
    for (uint32_t spin = 0; spin < max_spins; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
        if (xhci_trb_type(control) != XHCI_TRB_TRANSFER_EVENT) {
            continue;
        }
        {
            uint8_t event_slot_id = (uint8_t)((control >> 24) & 0xffu);
            uint8_t event_endpoint_id = (uint8_t)((control >> 16) & 0x1fu);
            uint32_t completion = (event.status >> 24) & 0xffu;
            uint64_t event_trb_phys = ((uint64_t)event.parameter_hi << 32) | event.parameter_lo;

            if (event_slot_id == slot_id && event_endpoint_id == endpoint_id) {
                if (expected_trb_phys != 0u && event_trb_phys != expected_trb_phys) {
                    continue;
                }
                if (completion_out != 0) {
                    *completion_out = completion;
                }
                return 1;
            }
            (void)xhci_hid_defer_transfer_event(event_slot_id, event_endpoint_id, completion);
            continue;
        }
    }
    return 0;
}

static int xhci_command_enable_slot(uint8_t *slot_id_out) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(0u, 0u, XHCI_TRB_ENABLE_SLOT << 10);
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS ||
        slot_id == 0u) {
        kprint("xhci: enable slot failed cc=%u slot=%u\n", completion, (uint32_t)slot_id);
        return 0;
    }
    if (slot_id_out != 0) {
        *slot_id_out = slot_id;
    }
    return 1;
}

static int xhci_command_address_device(struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(dev->input_context_phys,
                      0u,
                      (XHCI_TRB_ADDRESS_DEVICE << 10) | ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: address device failed port=%u slot=%u cc=%u event_slot=%u\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        return 0;
    }
    return 1;
}

static int xhci_command_context(uint8_t command_type, struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(dev->input_context_phys,
                      0u,
                      (command_type << 10) | ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: context command %u failed slot=%u cc=%u event_slot=%u\n",
               command_type,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        return 0;
    }
    return 1;
}

static uint32_t xhci_ep0_max_packet_for_speed(uint8_t speed) {
    if (speed == XHCI_SPEED_LOW || speed == XHCI_SPEED_FULL) {
        return 8u;
    }
    if (speed >= XHCI_SPEED_SUPER) {
        return 512u;
    }
    return 64u;
}

static uint32_t xhci_ep0_max_packet_from_descriptor(uint8_t speed, uint8_t mps0) {
    if (speed >= XHCI_SPEED_SUPER) {
        return mps0 < 16u ? (1u << mps0) : 512u;
    }
    if (mps0 == 8u || mps0 == 16u || mps0 == 32u || mps0 == 64u) {
        return mps0;
    }
    return xhci_ep0_max_packet_for_speed(speed);
}

static int xhci_update_ep0_max_packet(struct xhci_enum_device *dev, uint32_t max_packet) {
    uint8_t *input_control;
    uint8_t *ep0;
    uint8_t *out_ep0;
    uint32_t *ic;
    uint32_t *ep0_ctx;

    if (dev == 0 || max_packet == 0u || max_packet > 512u) {
        return 0;
    }
    input_control = xhci_context_ptr(dev->input_context, 0u);
    ep0 = xhci_context_ptr(dev->input_context, 2u);
    out_ep0 = xhci_context_ptr(dev->device_context, 1u);
    ic = (uint32_t *)input_control;
    ep0_ctx = (uint32_t *)ep0;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(ep0, out_ep0, g_xhci.context_size);
    ic[1] = XHCI_EP0_FLAG;
    ep0_ctx[1] = (ep0_ctx[1] & ~(0xffffu << 16)) | (max_packet << 16);
    if (!xhci_command_context(XHCI_TRB_EVALUATE_CONTEXT, dev)) {
        kprint("xhci: slot%u EP0 mps update failed mps=%u\n",
               (uint32_t)dev->slot_id,
               max_packet);
        return 0;
    }
    return 1;
}

static void xhci_prepare_address_context(struct xhci_enum_device *dev) {
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *ep0 = xhci_context_ptr(dev->input_context, 2u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *ep0_ctx = (uint32_t *)ep0;
    uint32_t max_packet = xhci_ep0_max_packet_for_speed(dev->speed);

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memset(dev->device_context, 0, XHCI_PAGE_SIZE);
    memset(dev->ep0_ring, 0, XHCI_PAGE_SIZE);
    xhci_ring_trb_set(&dev->ep0_ring[XHCI_RING_TRBS - 1u],
                      dev->ep0_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    dev->ep0_enqueue = 0u;
    dev->ep0_cycle = 1u;
    ic[1] = XHCI_SLOT_FLAG | XHCI_EP0_FLAG;
    slot_ctx[0] = (dev->route_string & 0xfffffu) |
                  ((uint32_t)dev->speed << 20) |
                  XHCI_SLOT_LAST_CTX_1;
    slot_ctx[1] = (uint32_t)dev->port << 16;
    if (dev->parent_slot_id != 0u &&
        (dev->speed == XHCI_SPEED_FULL || dev->speed == XHCI_SPEED_LOW)) {
        slot_ctx[2] = ((uint32_t)dev->parent_slot_id) |
                      ((uint32_t)dev->parent_port << 8);
    }
    ep0_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (4u << 3) | (max_packet << 16);
    ep0_ctx[2] = (uint32_t)dev->ep0_ring_phys | 1u;
    ep0_ctx[3] = (uint32_t)(dev->ep0_ring_phys >> 32);
    ep0_ctx[4] = 8u;
}

static void xhci_ep0_ring_trb(struct xhci_enum_device *dev,
                              uint64_t parameter,
                              uint32_t status,
                              uint32_t control) {
    struct xhci_trb *trb = &dev->ep0_ring[dev->ep0_enqueue];

    xhci_ring_trb_set(trb, parameter, status, control | dev->ep0_cycle);
    dev->ep0_enqueue++;
    if (dev->ep0_enqueue >= XHCI_RING_TRBS - 1u) {
        dev->ep0_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (dev->ep0_cycle != 0u ? 1u : 0u);
        dev->ep0_cycle ^= 1u;
        dev->ep0_enqueue = 0u;
    }
}

static uint64_t xhci_transfer_ring_trb(struct xhci_trb *ring,
                                       uint64_t ring_phys,
                                       uint32_t *enqueue,
                                       uint8_t *cycle,
                                       uint64_t parameter,
                                       uint32_t status,
                                       uint32_t control) {
    struct xhci_trb *trb = &ring[*enqueue];
    uint64_t trb_phys = ring_phys + (uint64_t)(*enqueue) * XHCI_TRB_SIZE;

    xhci_ring_trb_set(trb, parameter, status, control | *cycle);
    (*enqueue)++;
    if (*enqueue >= XHCI_RING_TRBS - 1u) {
        ring[XHCI_RING_TRBS - 1u].parameter_lo = (uint32_t)ring_phys;
        ring[XHCI_RING_TRBS - 1u].parameter_hi = (uint32_t)(ring_phys >> 32);
        ring[XHCI_RING_TRBS - 1u].status = 0u;
        ring[XHCI_RING_TRBS - 1u].control = (XHCI_TRB_LINK << 10) | 2u | (*cycle != 0u ? 1u : 0u);
        *cycle ^= 1u;
        *enqueue = 0u;
    }
    return trb_phys;
}

static uint64_t xhci_setup_packet(uint8_t request_type,
                                  uint8_t request,
                                  uint16_t value,
                                  uint16_t index,
                                  uint16_t length) {
    return (uint64_t)request_type |
           ((uint64_t)request << 8) |
           ((uint64_t)value << 16) |
           ((uint64_t)index << 32) |
           ((uint64_t)length << 48);
}

static int xhci_control_transfer(struct xhci_enum_device *dev,
                                 uint8_t request_type,
                                 uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 void *buffer,
                                 uint16_t length,
                                 uint8_t data_in) {
    uint64_t setup;
    uint32_t completion = 0u;
    uint32_t setup_type;
    uint32_t status_control;
    uint32_t wait_spins;
    uint8_t quiet_failure;

    if (dev == 0 || length > XHCI_PAGE_SIZE || (length != 0u && buffer == 0)) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    memset(dev->descriptor, 0, XHCI_PAGE_SIZE);
    if (buffer != 0 && length != 0u && !data_in) {
        memcpy(dev->descriptor, buffer, length);
    }
    setup_type = length == 0u ? 0u : (data_in ? 3u : 2u);
    setup = xhci_setup_packet(request_type,
                              request,
                              value,
                              index,
                              length);
    xhci_ep0_ring_trb(dev,
                      setup,
                      8u,
                      (XHCI_TRB_SETUP_STAGE << 10) | (1u << 6) | (setup_type << 16));
    if (length != 0u) {
        xhci_ep0_ring_trb(dev,
                          dev->descriptor_phys,
                          length,
                          (XHCI_TRB_DATA_STAGE << 10) | (data_in ? (1u << 16) : 0u));
    }
    status_control = (XHCI_TRB_STATUS_STAGE << 10) | (1u << 5);
    if (!data_in) {
        status_control |= 1u << 16;
    }
    xhci_ep0_ring_trb(dev,
                      0u,
                      0u,
                      status_control);
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, 1u);
    wait_spins = request_type == 0xa1u && request == USB_REQ_GET_REPORT
                     ? XHCI_HID_REPORT_WAIT_SPINS
                     : XHCI_WAIT_SPINS_DEFAULT;
    if (request_type == 0xa3u && request == USB_REQ_GET_STATUS) {
        wait_spins = XHCI_HUB_STATUS_WAIT_SPINS;
    }
    quiet_failure = (request_type == 0xa1u && request == USB_REQ_GET_REPORT) ||
                    (request_type == 0xa3u && request == USB_REQ_GET_STATUS);
    if (!xhci_wait_transfer_event_spins(dev->slot_id, 1u, &completion, 0u, wait_spins) ||
        completion != XHCI_CC_SUCCESS) {
        if (!quiet_failure) {
            kprint("xhci: control failed port=%u slot=%u req=%u type=%x cc=%u\n",
                   (uint32_t)dev->port,
                   (uint32_t)dev->slot_id,
                   (uint32_t)request,
                   (uint32_t)request_type,
                   completion);
        }
        xhci_save_active_controller();
        return 0;
    }
    if (buffer != 0 && length != 0u && data_in) {
        memcpy(buffer, dev->descriptor, length);
    }
    xhci_save_active_controller();
    return 1;
}

static int xhci_control_get_descriptor(struct xhci_enum_device *dev,
                                       uint8_t desc_type,
                                       uint8_t desc_index,
                                       void *buffer,
                                       uint16_t length) {
    for (uint32_t attempt = 0u; attempt < XHCI_DESCRIPTOR_RETRIES; attempt++) {
        if (xhci_control_transfer(dev,
                                  0x80u,
                                  USB_REQ_GET_DESCRIPTOR,
                                  (uint16_t)(((uint16_t)desc_type << 8) | desc_index),
                                  0u,
                                  buffer,
                                  length,
                                  1u)) {
            return 1;
        }
        xhci_delay_ms(10u);
    }
    return 0;
}

static int xhci_control_set_configuration(struct xhci_enum_device *dev, uint8_t configuration) {
    return xhci_control_transfer(dev,
                                 0x00u,
                                 USB_REQ_SET_CONFIGURATION,
                                 configuration,
                                 0u,
                                 0,
                                 0u,
                                 0u);
}

static void xhci_write_msc_name(char *dst, uint32_t index) {
    dst[0] = 'x';
    dst[1] = 'u';
    dst[2] = 's';
    dst[3] = 'b';
    dst[4] = 'm';
    dst[5] = 's';
    dst[6] = 'c';
    dst[7] = (char)('0' + (index % 10u));
    dst[8] = '\0';
}

static int xhci_parse_hid_keyboard_config(struct xhci_enum_device *dev,
                                          struct xhci_hid_keyboard *kbd,
                                          const uint8_t *cfg,
                                          uint32_t length) {
    uint32_t offset = 0u;
    uint8_t in_hid = 0u;
    uint8_t hid_iface = 0u;
    uint8_t hid_subclass = 0u;
    uint8_t hid_protocol = 0u;
    uint8_t hid_endpoint_count = 0u;
    uint8_t found = 0u;
    uint8_t found_boot = 0u;

    if (dev == 0 || kbd == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            hid_iface = cfg[offset + 2u];
            hid_endpoint_count = cfg[offset + 4u];
            hid_subclass = cfg[offset + 6u];
            hid_protocol = cfg[offset + 7u];
            in_hid = cfg[offset + 5u] == USB_CLASS_HID;
            if (in_hid) {
                kprint("xhci: slot%u HID iface=%u subclass=%u proto=%u eps=%u\n",
                       (uint32_t)dev->slot_id,
                       (uint32_t)hid_iface,
                       (uint32_t)hid_subclass,
                       (uint32_t)hid_protocol,
                       (uint32_t)hid_endpoint_count);
            }
        } else if (type == 5u && len >= 7u && in_hid) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = (uint16_t)(usb_read_u16le(cfg + offset + 4u) & 0x07ffu);
            uint8_t interval = cfg[offset + 6u];
            uint8_t ep_num = ep & 0x0fu;
            uint8_t is_boot_keyboard = hid_subclass == USB_SUBCLASS_BOOT &&
                                       hid_protocol == USB_PROTO_KEYBOARD;

            kprint("xhci: slot%u HID iface=%u ep=%x attr=%u mps=%u interval=%u\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)hid_iface,
                   (uint32_t)ep,
                   (uint32_t)attr,
                   (uint32_t)mps,
                   (uint32_t)interval);

            if (attr == 3u && (ep & 0x80u) != 0u && ep_num != 0u && mps != 0u &&
                (!found || (is_boot_keyboard && !found_boot))) {
                kbd->interface_number = hid_iface;
                kbd->interface_class = USB_CLASS_HID;
                kbd->interface_subclass = hid_subclass;
                kbd->interface_protocol = hid_protocol;
                kbd->boot_protocol = is_boot_keyboard;
                kbd->interrupt_in_ep = ep;
                kbd->interrupt_in_epid = (uint8_t)(ep_num * 2u + 1u);
                kbd->interrupt_in_mps = mps;
                kbd->interrupt_in_interval = interval;
                kbd->report_size = (uint8_t)(mps > XHCI_HID_REPORT_BYTES ? XHCI_HID_REPORT_BYTES : mps);
                if (kbd->report_size < 8u) {
                    kbd->report_size = 8u;
                }
                found = 1u;
                found_boot = is_boot_keyboard;
            }
        }
        offset += len;
    }
    return found;
}

static int xhci_hid_set_protocol(struct xhci_hid_keyboard *kbd, uint8_t protocol) {
    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    return xhci_control_transfer(kbd->dev,
                                 0x21u,
                                 USB_REQ_SET_PROTOCOL,
                                 protocol,
                                 kbd->interface_number,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hid_set_idle(struct xhci_hid_keyboard *kbd) {
    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    return xhci_control_transfer(kbd->dev,
                                 0x21u,
                                 USB_REQ_SET_IDLE,
                                 0u,
                                 kbd->interface_number,
                                 0,
                                 0u,
                                 0u);
}

static uint8_t xhci_hid_interval_value(struct xhci_enum_device *dev, uint8_t interval) {
    uint32_t target;
    uint8_t encoded = 0u;

    if (interval == 0u) {
        interval = 10u;
    }
    if (dev != 0 && dev->speed >= XHCI_SPEED_HIGH) {
        return interval > 16u ? 15u : (uint8_t)(interval - 1u);
    }
    target = (uint32_t)interval * 8u;
    if (target < 8u) {
        target = 8u;
    }
    target--;
    while (target != 0u && encoded < 15u) {
        target >>= 1;
        encoded++;
    }
    return encoded;
}

static void xhci_prepare_hid_interrupt_context(struct xhci_hid_keyboard *kbd) {
    struct xhci_enum_device *dev = kbd->dev;
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(dev->device_context, 0u);
    uint8_t *ep = xhci_context_ptr(dev->input_context, (uint32_t)kbd->interrupt_in_epid + 1u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *ep_ctx = (uint32_t *)ep;
    uint32_t interval = xhci_hid_interval_value(dev, kbd->interrupt_in_interval);
    uint32_t mps = kbd->interrupt_in_mps;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG | (1u << kbd->interrupt_in_epid);
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << 27)) | ((uint32_t)kbd->interrupt_in_epid << 27);

    ep_ctx[0] = interval << 16;
    ep_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (7u << 3) | (mps << 16);
    ep_ctx[2] = (uint32_t)kbd->interrupt_in_ring_phys | 1u;
    ep_ctx[3] = (uint32_t)(kbd->interrupt_in_ring_phys >> 32);
    ep_ctx[4] = (uint32_t)kbd->report_size | (mps << 16);
}

static int xhci_configure_hid_interrupt_endpoint(struct xhci_hid_keyboard *kbd) {
    if (kbd == 0 || kbd->dev == 0 || kbd->interrupt_in_epid == 0u) {
        return 0;
    }
    xhci_prepare_hid_interrupt_context(kbd);
    return xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, kbd->dev);
}

static uint32_t xhci_hid_report_transfer_size(const struct xhci_hid_keyboard *kbd) {
    uint32_t report_size;

    if (kbd == 0) {
        return 8u;
    }
    report_size = kbd->report_size;
    if (report_size < 8u || report_size > XHCI_HID_REPORT_BYTES) {
        report_size = 8u;
    }
    return report_size;
}

static void xhci_hid_copy_report(const struct xhci_hid_keyboard *kbd, uint8_t report[8]) {
    uint32_t report_offset = 0u;
    uint32_t report_size = xhci_hid_report_transfer_size(kbd);

    if (report == 0) {
        return;
    }
    memset(report, 0, 8u);
    if (kbd == 0 || kbd->report == 0) {
        return;
    }
    if (!kbd->boot_protocol && report_size > 8u && kbd->report[0] != 0u) {
        report_offset = 1u;
    }
    if (report_offset + 8u <= report_size) {
        memcpy(report, kbd->report + report_offset, 8u);
    } else {
        memcpy(report, kbd->report, report_size < 8u ? report_size : 8u);
    }
}

static void xhci_hid_queue_report(struct xhci_hid_keyboard *kbd, uint32_t completion) {
    uint32_t index;

    if (kbd == 0) {
        return;
    }
    if (kbd->report_queue_count >= XHCI_HID_REPORT_QUEUE_SIZE) {
        kbd->report_queue_tail = (uint8_t)((kbd->report_queue_tail + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
        kbd->report_queue_count--;
    }
    index = kbd->report_queue_head;
    kbd->report_queue_completion[index] = (uint8_t)completion;
    if (completion == XHCI_CC_SUCCESS || completion == XHCI_CC_SHORT_PACKET) {
        xhci_hid_copy_report(kbd, kbd->report_queue[index]);
    } else {
        memset(kbd->report_queue[index], 0, 8u);
    }
    kbd->report_queue_head = (uint8_t)((kbd->report_queue_head + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
    kbd->report_queue_count++;
}

static int xhci_hid_pop_queued_report(struct xhci_hid_keyboard *kbd,
                                      uint8_t report[8],
                                      uint32_t *completion_out) {
    uint32_t index;

    if (kbd == 0 || report == 0 || kbd->report_queue_count == 0u) {
        return 0;
    }
    index = kbd->report_queue_tail;
    memcpy(report, kbd->report_queue[index], 8u);
    if (completion_out != 0) {
        *completion_out = kbd->report_queue_completion[index];
    }
    kbd->report_queue_tail = (uint8_t)((kbd->report_queue_tail + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
    kbd->report_queue_count--;
    return 1;
}

static int xhci_hid_submit_interrupt_report(struct xhci_hid_keyboard *kbd) {
    uint32_t report_size;

    if (kbd == 0 || kbd->dev == 0 || kbd->interrupt_in_ring == 0 || kbd->report == 0) {
        return 0;
    }
    if (kbd->interrupt_pending) {
        return 1;
    }
    if (!xhci_select_controller(kbd->dev->controller_index)) {
        return 0;
    }
    report_size = xhci_hid_report_transfer_size(kbd);
    memset(kbd->report, 0, report_size);
    (void)xhci_transfer_ring_trb(kbd->interrupt_in_ring,
                                 kbd->interrupt_in_ring_phys,
                                 &kbd->interrupt_in_enqueue,
                                 &kbd->interrupt_in_cycle,
                                 kbd->report_phys,
                                 report_size,
                                 (XHCI_TRB_NORMAL << 10) | (1u << 5) | (1u << 2));
    kbd->interrupt_pending = 1u;
    xhci_write32(g_xhci.doorbell, (uint32_t)kbd->dev->slot_id * 4u, kbd->interrupt_in_epid);
    xhci_save_active_controller();
    return 1;
}

static int xhci_hid_poll_interrupt_report(struct xhci_hid_keyboard *kbd, uint8_t report[8]) {
    uint32_t completion = 0u;
    uint8_t from_queue = 0u;

    if (kbd == 0 || kbd->dev == 0 || report == 0 || kbd->interrupt_in_ring == 0 || kbd->report == 0) {
        return 0;
    }
    if (!xhci_select_controller(kbd->dev->controller_index)) {
        return 0;
    }
    if (xhci_hid_pop_queued_report(kbd, report, &completion)) {
        from_queue = 1u;
        goto complete_transfer;
    }
    if (!xhci_hid_submit_interrupt_report(kbd)) {
        xhci_save_active_controller();
        return 0;
    }
    if (!xhci_wait_transfer_event_spins(kbd->dev->slot_id,
                                        kbd->interrupt_in_epid,
                                        &completion,
                                        0u,
                                        XHCI_HID_REPORT_WAIT_SPINS)) {
        xhci_save_active_controller();
        return 0;
    }
    kbd->interrupt_pending = 0u;
complete_transfer:
    if (completion != XHCI_CC_SUCCESS && completion != XHCI_CC_SHORT_PACKET) {
        if (!kbd->report_fail_logged) {
            kprint("xhci: hidkbd interrupt completion cc=%u epid=%u\n",
                   completion,
                   (uint32_t)kbd->interrupt_in_epid);
            kbd->report_fail_logged = 1u;
        }
        xhci_hid_release_all_keys(kbd);
        (void)xhci_hid_submit_interrupt_report(kbd);
        xhci_save_active_controller();
        return 0;
    }
    if (!from_queue) {
        xhci_hid_copy_report(kbd, report);
    }
    (void)xhci_hid_submit_interrupt_report(kbd);
    xhci_save_active_controller();
    return 1;
}

static void xhci_hid_queue_event(struct keyboard_event event);
static int xhci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage);

static int xhci_hid_set_leds(struct xhci_hid_keyboard *kbd, uint8_t led_state) {
    uint8_t output = led_state & 0x07u;

    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    if (xhci_control_transfer(kbd->dev,
                              0x21u,
                              USB_REQ_SET_REPORT,
                              0x0200u,
                              kbd->interface_number,
                              &output,
                              1u,
                              0u)) {
        kbd->led_state = output;
        kbd->led_fail_logged = 0u;
        return 1;
    }
    if (!kbd->led_fail_logged) {
        kprint("xhci: hidkbd LED update failed iface=%u leds=%x\n",
               (uint32_t)kbd->interface_number,
               (uint32_t)output);
        kbd->led_fail_logged = 1u;
    }
    return 0;
}

static int xhci_keycode_is_lock(enum keyboard_keycode keycode) {
    return keycode == KEYBOARD_KEY_CAPS_LOCK ||
           keycode == KEYBOARD_KEY_NUM_LOCK ||
           keycode == KEYBOARD_KEY_SCROLL_LOCK;
}

static void xhci_hid_queue_key_event(struct xhci_hid_keyboard *kbd,
                                     enum keyboard_keycode keycode,
                                     int pressed) {
    uint8_t old_leds = keyboard_led_state();
    struct keyboard_event event = keyboard_handle_keycode(keycode, pressed);
    uint8_t new_leds = keyboard_led_state();

    xhci_hid_queue_event(event);
    if (pressed && old_leds != new_leds && xhci_keycode_is_lock(keycode)) {
        (void)xhci_hid_set_leds(kbd, new_leds);
    }
}

static void xhci_hid_set_repeat_usage(struct xhci_hid_keyboard *kbd, uint8_t usage) {
    if (kbd == 0 || kbd->repeat_usage == usage) {
        return;
    }
    kbd->repeat_usage = usage;
    kbd->repeat_active = usage != 0u ? 1u : 0u;
    kbd->repeat_ticks = 0u;
}

static void xhci_hid_tick_repeat(struct xhci_hid_keyboard *kbd) {
    uint32_t repeat_age;

    if (kbd == 0 || !kbd->present || kbd->poll_disabled || !kbd->repeat_active || kbd->repeat_usage == 0u) {
        return;
    }
    if (!xhci_hid_report_contains_usage(kbd->last_report, kbd->repeat_usage)) {
        xhci_hid_set_repeat_usage(kbd, 0u);
        return;
    }
    kbd->repeat_ticks++;
    if (kbd->repeat_ticks < XHCI_HID_REPEAT_DELAY_TICKS) {
        return;
    }
    repeat_age = kbd->repeat_ticks - XHCI_HID_REPEAT_DELAY_TICKS;
    if ((repeat_age % XHCI_HID_REPEAT_RATE_TICKS) != 0u) {
        return;
    }
    xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(kbd->repeat_usage), 1);
}

static void xhci_hid_tick_repeats_once(void) {
    uint32_t tick = hal_timer_current_ticks();

    if (tick == g_hid_last_repeat_tick) {
        return;
    }
    g_hid_last_repeat_tick = tick;
    for (uint32_t i = 0u; i < g_hid_keyboard_count; i++) {
        xhci_hid_tick_repeat(&g_hid_keyboards[i]);
    }
}

static int xhci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage) {
    for (uint32_t i = 2u; i < 8u; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void xhci_hid_queue_event(struct keyboard_event event) {
    if (event.keycode == KEYBOARD_KEY_NONE) {
        return;
    }
    if (g_hid_event_count >= XHCI_HID_EVENT_QUEUE_SIZE) {
        g_hid_event_tail = (g_hid_event_tail + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
        g_hid_event_count--;
    }
    g_hid_event_queue[g_hid_event_head] = event;
    g_hid_event_head = (g_hid_event_head + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count++;
}

static int xhci_hid_pop_event(struct keyboard_event *out) {
    if (out == 0 || g_hid_event_count == 0u) {
        return 0;
    }
    *out = g_hid_event_queue[g_hid_event_tail];
    g_hid_event_tail = (g_hid_event_tail + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count--;
    return 1;
}

static int xhci_hid_defer_transfer_event(uint8_t slot_id, uint8_t endpoint_id, uint32_t completion) {
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (!kbd->present ||
            !kbd->interrupt_pending ||
            kbd->dev == 0 ||
            kbd->dev->controller_index != g_xhci_active_controller ||
            kbd->dev->slot_id != slot_id ||
            kbd->interrupt_in_epid != endpoint_id) {
            continue;
        }
        kbd->interrupt_pending = 0u;
        xhci_hid_queue_report(kbd, completion);
        (void)xhci_hid_submit_interrupt_report(kbd);
        return 1;
    }
    return 0;
}

static void xhci_hid_process_report(struct xhci_hid_keyboard *kbd, const uint8_t report[8]) {
    static const uint8_t modifier_usages[8] = {0xe0u, 0xe1u, 0xe2u, 0xe3u, 0xe4u, 0xe5u, 0xe6u, 0xe7u};
    uint8_t repeat_usage;

    if (kbd == 0 || report == 0) {
        return;
    }
    repeat_usage = kbd->repeat_usage;
    for (uint32_t i = 0u; i < 8u; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        enum keyboard_keycode keycode = usb_hid_usage_to_keycode(modifier_usages[i]);

        if (((kbd->last_report[0] ^ report[0]) & mask) == 0u) {
            continue;
        }
        xhci_hid_queue_key_event(kbd, keycode, (report[0] & mask) != 0u);
    }
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t usage = kbd->last_report[i];

        if (usage != 0u && !xhci_hid_report_contains_usage(report, usage)) {
            xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(usage), 0);
        }
    }
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t usage = report[i];

        if (usage != 0u && !xhci_hid_report_contains_usage(kbd->last_report, usage)) {
            xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(usage), 1);
            if (usb_hid_usage_can_repeat(usage)) {
                repeat_usage = usage;
            }
        }
    }
    if (repeat_usage == 0u) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    } else if (!xhci_hid_report_contains_usage(report, repeat_usage)) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    }
    xhci_hid_set_repeat_usage(kbd, repeat_usage);
    memcpy(kbd->last_report, report, 8u);
}

static void xhci_hid_release_all_keys(struct xhci_hid_keyboard *kbd) {
    uint8_t empty_report[8];
    uint8_t had_keys = 0u;

    if (kbd == 0) {
        return;
    }
    for (uint32_t i = 0u; i < 8u; i++) {
        if (kbd->last_report[i] != 0u) {
            had_keys = 1u;
            break;
        }
    }
    if (had_keys) {
        memset(empty_report, 0, sizeof(empty_report));
        xhci_hid_process_report(kbd, empty_report);
    }
    kbd->repeat_active = 0u;
    kbd->repeat_usage = 0u;
    kbd->repeat_ticks = 0u;
    memset(kbd->last_report, 0, sizeof(kbd->last_report));
}

static int xhci_config_has_hub_interface(const uint8_t *cfg, uint32_t length) {
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

static int xhci_parse_msc_config(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_msc = 0;
    uint8_t saw_msc = 0;
    uint8_t saw_uas = 0;

    if (dev == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
    dev->msc_interface_number = 0u;
    dev->bulk_in_ep = 0u;
    dev->bulk_out_ep = 0u;
    dev->bulk_in_epid = 0u;
    dev->bulk_out_epid = 0u;
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
            if (cfg[offset + 5u] == USB_CLASS_MASS_STORAGE) {
                saw_msc = 1u;
                saw_uas = cfg[offset + 7u] == 0x62u ? 1u : saw_uas;
                if (in_msc) {
                    dev->msc_interface_number = cfg[offset + 2u];
                }
                kprint("xhci: slot%u MSC iface=%u subclass=%u proto=%x eps=%u bot=%u\n",
                       (uint32_t)dev->slot_id,
                       (uint32_t)cfg[offset + 2u],
                       (uint32_t)cfg[offset + 6u],
                       (uint32_t)cfg[offset + 7u],
                       (uint32_t)cfg[offset + 4u],
                       (uint32_t)in_msc);
            }
        } else if (type == 5u && len >= 7u && in_msc) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = (uint16_t)(usb_read_u16le(cfg + offset + 4u) & 0x07ffu);
            uint8_t ep_num = ep & 0x0fu;

            if (attr == 2u && ep_num != 0u && (ep & 0x80u) != 0u) {
                dev->bulk_in_ep = ep;
                dev->bulk_in_epid = (uint8_t)(ep_num * 2u + 1u);
                dev->bulk_in_mps = mps;
            } else if (attr == 2u && ep_num != 0u) {
                dev->bulk_out_ep = ep;
                dev->bulk_out_epid = (uint8_t)(ep_num * 2u);
                dev->bulk_out_mps = mps;
            }
        }
        offset += len;
    }
    if (dev->configuration != 0u &&
        dev->bulk_in_epid != 0u &&
        dev->bulk_out_epid != 0u &&
        dev->bulk_in_mps != 0u &&
        dev->bulk_out_mps != 0u) {
        return 1;
    }
    if (saw_msc) {
        kprint("xhci: slot%u MSC unsupported or incomplete in=%x/%u out=%x/%u uas=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)saw_uas);
    }
    return 0;
}

static int xhci_hub_get_descriptor(struct xhci_enum_device *hub,
                                   uint8_t desc_type,
                                   uint8_t *buffer,
                                   uint16_t length) {
    return xhci_control_transfer(hub,
                                 0xa0u,
                                 USB_REQ_GET_DESCRIPTOR,
                                 (uint16_t)(desc_type << 8),
                                 0u,
                                 buffer,
                                 length,
                                 1u);
}

static int xhci_hub_set_port_feature(struct xhci_enum_device *hub, uint8_t port, uint16_t feature) {
    return xhci_control_transfer(hub,
                                 0x23u,
                                 USB_REQ_SET_FEATURE,
                                 feature,
                                 port,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hub_clear_port_feature(struct xhci_enum_device *hub, uint8_t port, uint16_t feature) {
    return xhci_control_transfer(hub,
                                 0x23u,
                                 USB_REQ_CLEAR_FEATURE,
                                 feature,
                                 port,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hub_get_port_status(struct xhci_enum_device *hub,
                                    uint8_t port,
                                    uint16_t *status_out,
                                    uint16_t *change_out) {
    uint8_t data[4];

    memset(data, 0, sizeof(data));
    if (!xhci_control_transfer(hub,
                               0xa3u,
                               USB_REQ_GET_STATUS,
                               0u,
                               port,
                               data,
                               sizeof(data),
                               1u)) {
        return 0;
    }
    if (status_out != 0) {
        *status_out = usb_read_u16le(data);
    }
    if (change_out != 0) {
        *change_out = usb_read_u16le(data + 2);
    }
    return 1;
}

static uint8_t xhci_hub_child_speed(struct xhci_enum_device *hub, uint16_t status) {
    if ((status & USB_HUB_PORT_LOW_SPEED) != 0u) {
        return XHCI_SPEED_LOW;
    }
    if ((status & USB_HUB_PORT_HIGH_SPEED) != 0u) {
        return XHCI_SPEED_HIGH;
    }
    if (hub->speed >= XHCI_SPEED_SUPER) {
        return XHCI_SPEED_SUPER;
    }
    return XHCI_SPEED_FULL;
}

static void xhci_hub_clear_changes(struct xhci_enum_device *hub, uint8_t port, uint16_t change) {
    if ((change & USB_HUB_PORT_CONNECTION) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION);
    }
    if ((change & USB_HUB_PORT_ENABLE) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_ENABLE);
    }
    if ((change & USB_HUB_PORT_RESET) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_RESET);
    }
}

static void xhci_prepare_hub_context(struct xhci_enum_device *hub, uint8_t port_count) {
    uint8_t *input_control = xhci_context_ptr(hub->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(hub->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(hub->device_context, 0u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;

    memset(hub->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG;
    slot_ctx[0] |= XHCI_SLOT_HUB;
    slot_ctx[1] = (slot_ctx[1] & ~(0xffu << 24)) | ((uint32_t)port_count << 24);
}

static int xhci_update_hub_context(struct xhci_enum_device *hub, uint8_t port_count) {
    xhci_prepare_hub_context(hub, port_count);
    if (xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, hub)) {
        return 1;
    }
    xhci_prepare_hub_context(hub, port_count);
    return xhci_command_context(XHCI_TRB_EVALUATE_CONTEXT, hub);
}

static void xhci_prepare_bulk_context(struct xhci_enum_device *dev) {
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(dev->device_context, 0u);
    uint8_t *bulk_out = xhci_context_ptr(dev->input_context, (uint32_t)dev->bulk_out_epid + 1u);
    uint8_t *bulk_in = xhci_context_ptr(dev->input_context, (uint32_t)dev->bulk_in_epid + 1u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *out_ctx = (uint32_t *)bulk_out;
    uint32_t *in_ctx = (uint32_t *)bulk_in;
    uint8_t last_epid = dev->bulk_in_epid > dev->bulk_out_epid ? dev->bulk_in_epid : dev->bulk_out_epid;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG |
            (1u << dev->bulk_out_epid) |
            (1u << dev->bulk_in_epid);
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << 27)) | ((uint32_t)last_epid << 27);

    out_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (2u << 3) | ((uint32_t)dev->bulk_out_mps << 16);
    out_ctx[2] = (uint32_t)dev->bulk_out_ring_phys | 1u;
    out_ctx[3] = (uint32_t)(dev->bulk_out_ring_phys >> 32);
    out_ctx[4] = dev->bulk_out_mps;

    in_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (6u << 3) | ((uint32_t)dev->bulk_in_mps << 16);
    in_ctx[2] = (uint32_t)dev->bulk_in_ring_phys | 1u;
    in_ctx[3] = (uint32_t)(dev->bulk_in_ring_phys >> 32);
    in_ctx[4] = dev->bulk_in_mps;
}

static int xhci_configure_bulk_endpoints(struct xhci_enum_device *dev) {
    xhci_prepare_bulk_context(dev);
    return xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, dev);
}

static int xhci_bulk_endpoint_ring(struct xhci_enum_device *dev,
                                   uint8_t endpoint_id,
                                   struct xhci_trb **ring_out,
                                   uint64_t *ring_phys_out,
                                   uint32_t **enqueue_out,
                                   uint8_t **cycle_out) {
    if (dev == 0 || ring_out == 0 || ring_phys_out == 0 || enqueue_out == 0 || cycle_out == 0) {
        return 0;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        *ring_out = dev->bulk_in_ring;
        *ring_phys_out = dev->bulk_in_ring_phys;
        *enqueue_out = &dev->bulk_in_enqueue;
        *cycle_out = &dev->bulk_in_cycle;
        return 1;
    }
    if (endpoint_id == dev->bulk_out_epid) {
        *ring_out = dev->bulk_out_ring;
        *ring_phys_out = dev->bulk_out_ring_phys;
        *enqueue_out = &dev->bulk_out_enqueue;
        *cycle_out = &dev->bulk_out_cycle;
        return 1;
    }
    return 0;
}

static void xhci_reset_transfer_ring(struct xhci_trb *ring,
                                     uint64_t ring_phys,
                                     uint32_t *enqueue,
                                     uint8_t *cycle) {
    if (ring == 0 || enqueue == 0 || cycle == 0) {
        return;
    }
    memset(ring, 0, XHCI_PAGE_SIZE);
    xhci_ring_trb_set(&ring[XHCI_RING_TRBS - 1u],
                      ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    *enqueue = 0u;
    *cycle = 1u;
}

static int xhci_command_endpoint(uint8_t command_type,
                                 struct xhci_enum_device *dev,
                                 uint8_t endpoint_id,
                                 uint64_t parameter) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    if (dev == 0 || endpoint_id == 0u) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    xhci_ring_command(parameter,
                      0u,
                      ((uint32_t)command_type << 10) |
                      ((uint32_t)endpoint_id << 16) |
                      ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: endpoint command %u failed slot=%u epid=%u cc=%u event_slot=%u\n",
               (uint32_t)command_type,
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               completion,
               (uint32_t)slot_id);
        xhci_save_active_controller();
        return 0;
    }
    xhci_save_active_controller();
    return 1;
}

static uint8_t xhci_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    uint8_t *endpoint;
    uint32_t *ctx;

    if (dev == 0 || dev->device_context == 0 || endpoint_id == 0u) {
        return XHCI_EP_STATE_DISABLED;
    }
    endpoint = xhci_context_ptr(dev->device_context, endpoint_id);
    ctx = (uint32_t *)endpoint;
    return (uint8_t)(ctx[0] & 0x7u);
}

static int xhci_stop_endpoint_if_running(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    uint8_t state = xhci_endpoint_state(dev, endpoint_id);

    if (state == XHCI_EP_STATE_RUNNING) {
        return xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u);
    }
    return state == XHCI_EP_STATE_STOPPED ||
           state == XHCI_EP_STATE_HALTED ||
           state == XHCI_EP_STATE_ERROR;
}

static int xhci_reset_bulk_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    struct xhci_trb *ring;
    uint64_t ring_phys;
    uint32_t *enqueue;
    uint8_t *cycle;
    uint8_t state;

    if (!xhci_bulk_endpoint_ring(dev, endpoint_id, &ring, &ring_phys, &enqueue, &cycle)) {
        return 0;
    }
    state = xhci_endpoint_state(dev, endpoint_id);
    if (state == XHCI_EP_STATE_RUNNING) {
        if (!xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u)) {
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state == XHCI_EP_STATE_HALTED) {
        if (!xhci_command_endpoint(XHCI_TRB_RESET_ENDPOINT, dev, endpoint_id, 0u)) {
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state != XHCI_EP_STATE_STOPPED && state != XHCI_EP_STATE_ERROR) {
        if (!xhci_stop_endpoint_if_running(dev, endpoint_id)) {
            return 0;
        }
    }
    xhci_reset_transfer_ring(ring, ring_phys, enqueue, cycle);
    return xhci_command_endpoint(XHCI_TRB_SET_TR_DEQUEUE, dev, endpoint_id, ring_phys | 1u);
}

static int xhci_enumerate_device(struct xhci_enum_device *dev);

static int xhci_enumerate_hub_port(struct xhci_enum_device *hub, uint8_t port) {
    struct xhci_enum_device *child;
    uint16_t status = 0u;
    uint16_t change = 0u;

    if (hub->route_depth >= 5u || port == 0u || port > 15u) {
        return 0;
    }
    if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
        kprint("xhci: hub slot%u port%u status failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    xhci_hub_clear_changes(hub, port, change);
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        return 0;
    }
    kprint("xhci: hub slot%u port%u connected status=%x change=%x\n",
           (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
    if (!xhci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
        kprint("xhci: hub slot%u port%u reset failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    xhci_delay_ms(80u);
    for (uint32_t i = 0; i < 200u; i++) {
        if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        xhci_delay_ms(1u);
    }
    xhci_hub_clear_changes(hub, port, change);
    if (!xhci_hub_get_port_status(hub, port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("xhci: hub slot%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
        return 0;
    }

    child = xhci_alloc_device_record();
    if (child == 0) {
        kprint("xhci: hub slot%u port%u no device slots\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    memset(child, 0, sizeof(*child));
    child->port = hub->port;
    child->controller_index = hub->controller_index;
    child->speed = xhci_hub_child_speed(hub, status);
    child->route_depth = (uint8_t)(hub->route_depth + 1u);
    child->route_string = hub->route_string | ((uint32_t)port << (hub->route_depth * 4u));
    child->parent_slot_id = hub->slot_id;
    child->parent_port = port;
    kprint("xhci: hub slot%u port%u route=%x speed=%u\n",
           (uint32_t)hub->slot_id,
           (uint32_t)port,
           child->route_string,
           (uint32_t)child->speed);
    return xhci_enumerate_device(child);
}

static void xhci_probe_hub(struct xhci_enum_device *dev, const uint8_t *cfg, uint16_t cfg_len) {
    uint8_t hub_desc[32];
    uint8_t primary_desc;
    uint8_t fallback_desc;
    uint8_t port_count;
    uint32_t pwr_ms;

    (void)cfg_len;
    if (!xhci_control_set_configuration(dev, cfg[5])) {
        kprint("xhci: hub slot%u set config failed\n", (uint32_t)dev->slot_id);
        return;
    }
    memset(hub_desc, 0, sizeof(hub_desc));
    primary_desc = dev->speed >= XHCI_SPEED_SUPER ? USB_DESC_SS_HUB : USB_DESC_HUB;
    fallback_desc = primary_desc == USB_DESC_HUB ? USB_DESC_SS_HUB : USB_DESC_HUB;
    if (!xhci_hub_get_descriptor(dev, primary_desc, hub_desc, primary_desc == USB_DESC_SS_HUB ? 12u : 8u) &&
        !xhci_hub_get_descriptor(dev, fallback_desc, hub_desc, fallback_desc == USB_DESC_SS_HUB ? 12u : 8u)) {
        kprint("xhci: hub slot%u descriptor failed\n", (uint32_t)dev->slot_id);
        return;
    }
    port_count = hub_desc[2];
    if (port_count > XHCI_MAX_HUB_PORTS) {
        port_count = XHCI_MAX_HUB_PORTS;
    }
    dev->hub_port_count = port_count;
    pwr_ms = (uint32_t)hub_desc[5] * 2u;
    if (pwr_ms < 100u) {
        pwr_ms = 100u;
    }
    kprint("xhci: hub slot%u rootport=%u ports=%u pwr=%u desc=%x\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev->port,
           (uint32_t)port_count,
           pwr_ms,
           (uint32_t)hub_desc[1]);
    if (!xhci_update_hub_context(dev, port_count)) {
        kprint("xhci: hub slot%u context update failed\n", (uint32_t)dev->slot_id);
        return;
    }
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)xhci_hub_set_port_feature(dev, port, USB_HUB_FEATURE_PORT_POWER);
    }
    xhci_delay_ms(pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        uint16_t status = 0u;
        uint16_t change = 0u;

        if (xhci_hub_get_port_status(dev, port, &status, &change)) {
            kprint("xhci: hub slot%u port%u status=%x change=%x\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)port,
                   (uint32_t)status,
                   (uint32_t)change);
        }
        (void)xhci_enumerate_hub_port(dev, port);
    }
}

static int xhci_bulk_transfer(struct xhci_enum_device *dev,
                              uint8_t endpoint_id,
                              uint64_t phys,
                              uint32_t bytes) {
    uint32_t completion = 0u;
    struct xhci_trb *ring;
    uint64_t ring_phys;
    uint64_t trb_phys;
    uint32_t trb_flags;
    uint32_t *enqueue;
    uint8_t *cycle;

    if (dev == 0 || endpoint_id == 0u || bytes > XHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_bulk_completion = 0u;
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        ring = dev->bulk_in_ring;
        ring_phys = dev->bulk_in_ring_phys;
        enqueue = &dev->bulk_in_enqueue;
        cycle = &dev->bulk_in_cycle;
    } else if (endpoint_id == dev->bulk_out_epid) {
        ring = dev->bulk_out_ring;
        ring_phys = dev->bulk_out_ring_phys;
        enqueue = &dev->bulk_out_enqueue;
        cycle = &dev->bulk_out_cycle;
    } else {
        return 0;
    }
    trb_flags = (XHCI_TRB_NORMAL << 10) | (1u << 5);
    if ((endpoint_id & 1u) != 0u) {
        trb_flags |= 1u << 2;
    }
    trb_phys = xhci_transfer_ring_trb(ring,
                                      ring_phys,
                                      enqueue,
                                      cycle,
                                      phys,
                                      bytes,
                                      trb_flags);
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, endpoint_id);
    (void)trb_phys;
    if (!xhci_wait_transfer_event_spins(dev->slot_id,
                                        endpoint_id,
                                        &completion,
                                        0u,
                                        XHCI_MSC_BULK_WAIT_SPINS)) {
        dev->last_bulk_completion = 0u;
        kprint("xhci: bulk timeout slot=%u epid=%u bytes=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               bytes);
        xhci_save_active_controller();
        return 0;
    }
    dev->last_bulk_completion = (uint8_t)completion;
    if (completion != XHCI_CC_SUCCESS &&
        !(completion == XHCI_CC_SHORT_PACKET && (endpoint_id & 1u) != 0u)) {
        kprint("xhci: bulk failed slot=%u epid=%u cc=%u bytes=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               completion,
               bytes);
        xhci_save_active_controller();
        return 0;
    }
    xhci_save_active_controller();
    return 1;
}

static int xhci_clear_endpoint_halt(struct xhci_enum_device *dev, uint8_t endpoint_address) {
    if (dev == 0 || endpoint_address == 0u) {
        return 0;
    }
    return xhci_control_transfer(dev,
                                 0x02u,
                                 USB_REQ_CLEAR_FEATURE,
                                 USB_FEATURE_ENDPOINT_HALT,
                                 endpoint_address,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_msc_bulk_only_reset(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    return xhci_control_transfer(dev,
                                 0x21u,
                                 USB_REQ_BULK_ONLY_RESET,
                                 0u,
                                 dev->msc_interface_number,
                                 0,
                                 0u,
                                 0u);
}

static void xhci_msc_recover_transport(struct xhci_enum_device *dev, uint8_t op, const char *phase) {
    if (dev == 0) {
        return;
    }
    kprint("xhci: MSC recovery slot=%u op=%x phase=%s in=%x out=%x\n",
           (uint32_t)dev->slot_id,
           (uint32_t)op,
           phase,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_out_ep);
    (void)xhci_msc_bulk_only_reset(dev);
    xhci_delay_ms(XHCI_MSC_RESET_SETTLE_MS);
    (void)xhci_clear_endpoint_halt(dev, dev->bulk_in_ep);
    xhci_delay_ms(20u);
    (void)xhci_clear_endpoint_halt(dev, dev->bulk_out_ep);
    xhci_delay_ms(20u);
    (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid);
    (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid);
    xhci_delay_ms(50u);
}

static int xhci_msc_command(struct xhci_enum_device *dev,
                            const uint8_t *cmd,
                            uint8_t cmd_len,
                            void *buffer,
                            uint32_t data_len,
                            uint8_t data_in) {
    uint32_t tag;
    uint32_t signature;
    uint32_t csw_tag;
    uint8_t csw_retry = 0u;

    if (dev == 0 || cmd == 0 || cmd_len == 0u || cmd_len > 16u || data_len > XHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_msc_phase = 0u;
    dev->last_msc_status = 0u;
    dev->last_msc_residue = 0u;
    memset(dev->cbw, 0, 31u);
    memset(dev->csw, 0, 13u);
    tag = ++dev->tag;
    usb_write_u32le(dev->cbw + 0, MSC_CBW_SIGNATURE);
    usb_write_u32le(dev->cbw + 4, tag);
    usb_write_u32le(dev->cbw + 8, data_len);
    dev->cbw[12] = data_in ? 0x80u : 0u;
    dev->cbw[13] = dev->msc_lun;
    dev->cbw[14] = cmd_len;
    memcpy(dev->cbw + 15, cmd, cmd_len);
    if (buffer != 0 && data_len != 0u && !data_in) {
        memcpy(dev->data, buffer, data_len);
    }
    dev->last_msc_phase = 1u;
    if (!xhci_bulk_transfer(dev, dev->bulk_out_epid, dev->cbw_phys, 31u)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("xhci: MSC CBW failed slot=%u tag=%x op=%x\n",
               (uint32_t)dev->slot_id,
               tag,
               (uint32_t)cmd[0]);
        xhci_msc_recover_transport(dev, cmd[0], "CBW");
        return 0;
    }
    xhci_delay_ms(5u);
    if (data_len != 0u) {
        if (data_in) {
            memset(dev->data, 0, data_len);
        }
        dev->last_msc_phase = 2u;
        if (!xhci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_epid : dev->bulk_out_epid,
                                dev->data_phys,
                                data_len)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("xhci: MSC DATA %s failed slot=%u tag=%x op=%x len=%u\n",
                   data_in ? "in" : "out",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   data_len);
            xhci_msc_recover_transport(dev, cmd[0], data_in ? "DATA-IN" : "DATA-OUT");
            return 0;
        }
        if (buffer != 0 && data_in) {
            memcpy(buffer, dev->data, data_len);
        }
        xhci_delay_ms(5u);
    }
    dev->last_msc_phase = 3u;
    for (;;) {
        memset(dev->csw, 0, 13u);
        if (xhci_bulk_transfer(dev, dev->bulk_in_epid, dev->csw_phys, 13u)) {
            break;
        }
        if (csw_retry != 0u) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("xhci: MSC CSW failed slot=%u tag=%x op=%x cc=%u\n",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   (uint32_t)dev->last_bulk_completion);
            xhci_msc_recover_transport(dev, cmd[0], "CSW");
            return 0;
        }
        (void)xhci_clear_endpoint_halt(dev, dev->bulk_in_ep);
        (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid);
        xhci_delay_ms(5u);
        csw_retry = 1u;
    }
    signature = usb_read_u32le(dev->csw);
    csw_tag = usb_read_u32le(dev->csw + 4);
    dev->last_msc_residue = usb_read_u32le(dev->csw + 8);
    dev->last_msc_status = dev->csw[12];
    if (signature != MSC_CSW_SIGNATURE || csw_tag != tag) {
        kprint("xhci: MSC CSW bad slot=%u op=%x sig=%x tag=%x/%x status=%u residue=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)cmd[0],
               signature,
               csw_tag,
               tag,
               (uint32_t)dev->last_msc_status,
               dev->last_msc_residue);
        xhci_msc_recover_transport(dev, cmd[0], "CSW-BAD");
        return 0;
    }
    if (dev->last_msc_status == 2u) {
        kprint("xhci: MSC CSW phase error slot=%u op=%x residue=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)cmd[0],
               dev->last_msc_residue);
        xhci_msc_recover_transport(dev, cmd[0], "CSW-PHASE");
        return 0;
    }
    if (dev->last_msc_status != 0u) {
        if (cmd[0] != SCSI_TEST_UNIT_READY &&
            cmd[0] != SCSI_READ_CAPACITY_10 &&
            cmd[0] != SCSI_REQUEST_SENSE) {
            kprint("xhci: MSC command status slot=%u op=%x status=%u residue=%x\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)cmd[0],
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
        }
        dev->last_msc_phase = 0u;
        return 0;
    }
    dev->last_msc_phase = 0u;
    return 1;
}

static int xhci_msc_read_capacity(struct xhci_enum_device *dev) {
    uint8_t cmd[10];
    uint8_t cap[8];
    uint32_t last_lba;
    uint32_t block_len;

    memset(cmd, 0, sizeof(cmd));
    memset(cap, 0, sizeof(cap));
    cmd[0] = SCSI_READ_CAPACITY_10;
    if (!xhci_msc_command(dev, cmd, 10u, cap, 8u, 1u)) {
        return 0;
    }
    last_lba = usb_read_u32be(cap);
    block_len = usb_read_u32be(cap + 4);
    if (block_len != XHCI_SECTOR_SIZE) {
        kprint("xhci: MSC unsupported block size=%x last_lba=%x\n", block_len, last_lba);
        return 0;
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return 1;
}

static int xhci_msc_test_unit_ready(struct xhci_enum_device *dev) {
    uint8_t cmd[6];

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return xhci_msc_command(dev, cmd, 6u, 0, 0u, 0u);
}

static void xhci_msc_clear_sense(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    dev->last_sense_key = 0u;
    dev->last_sense_asc = 0u;
    dev->last_sense_ascq = 0u;
}

static int xhci_msc_sense_response_valid(const uint8_t *sense, uint32_t length) {
    uint8_t response;

    if (sense == 0 || length < 14u) {
        return 0;
    }
    response = sense[0] & 0x7fu;
    return response == 0x70u ||
           response == 0x71u ||
           (sense[2] & 0x0fu) != 0u ||
           sense[12] != 0u ||
           sense[13] != 0u;
}

static void xhci_msc_record_sense(struct xhci_enum_device *dev, const uint8_t *sense, uint32_t length) {
    if (dev == 0 || !xhci_msc_sense_response_valid(sense, length)) {
        return;
    }
    dev->last_sense_key = sense[2] & 0x0fu;
    dev->last_sense_asc = sense[12];
    dev->last_sense_ascq = sense[13];
}

static int xhci_msc_medium_not_present(const struct xhci_enum_device *dev) {
    return dev != 0 && dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au;
}

static void xhci_msc_retry_delay(uint8_t failed_phase, uint8_t failed_status) {
    if (failed_status == MSC_STATUS_TRANSPORT_ERROR ||
        failed_phase == 1u ||
        failed_phase == 2u ||
        failed_phase == 3u) {
        xhci_delay_ms(120u);
        return;
    }
    xhci_delay_ms(20u);
}

static int xhci_msc_request_sense(struct xhci_enum_device *dev) {
    uint8_t cmd[6];
    uint8_t sense[18];
    int ok;

    memset(cmd, 0, sizeof(cmd));
    memset(sense, 0, sizeof(sense));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = sizeof(sense);
    ok = xhci_msc_command(dev, cmd, 6u, sense, sizeof(sense), 1u);
    if (ok || (dev != 0 && dev->last_msc_phase == 3u && xhci_msc_sense_response_valid(sense, sizeof(sense)))) {
        xhci_msc_record_sense(dev, sense, sizeof(sense));
        return 1;
    }
    return 0;
}

static uint8_t xhci_msc_get_max_lun(struct xhci_enum_device *dev) {
    uint8_t lun = 0u;

    if (dev == 0) {
        return 0u;
    }
    if (!xhci_control_transfer(dev,
                               0xa1u,
                               USB_REQ_GET_MAX_LUN,
                               0u,
                               dev->msc_interface_number,
                               &lun,
                               1u,
                               1u)) {
        return 0u;
    }
    if (lun > USB_MSC_MAX_LUN_LIMIT) {
        return 0u;
    }
    return lun;
}

static int xhci_msc_wait_ready(struct xhci_enum_device *dev) {
    for (uint32_t i = 0u; i < 20u; i++) {
        if (xhci_msc_test_unit_ready(dev)) {
            return 1;
        }
        if (xhci_msc_request_sense(dev) && xhci_msc_medium_not_present(dev)) {
            return 0;
        }
        xhci_delay_ms(100u);
    }
    return 0;
}

static int xhci_msc_probe_device(struct xhci_enum_device *dev) {
    uint8_t cmd[16];
    uint8_t inquiry[36];

    dev->max_lun = xhci_msc_get_max_lun(dev);
    kprint("xhci: MSC probe slot=%u if=%u maxlun=%u in=%x/%u out=%x/%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev->msc_interface_number,
           (uint32_t)dev->max_lun,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_in_mps,
           (uint32_t)dev->bulk_out_ep,
           (uint32_t)dev->bulk_out_mps);

    for (uint8_t lun = 0u; lun <= dev->max_lun; lun++) {
        uint8_t inquiry_ok = 0u;

        dev->msc_lun = lun;
        xhci_msc_clear_sense(dev);
        memset(cmd, 0, sizeof(cmd));
        memset(inquiry, 0, sizeof(inquiry));
        cmd[0] = SCSI_INQUIRY;
        cmd[4] = sizeof(inquiry);
        for (uint32_t i = 0u; i < 5u; i++) {
            if (xhci_msc_command(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
                inquiry_ok = 1u;
                break;
            }
            xhci_delay_ms(100u);
        }
        if (!inquiry_ok) {
            kprint("xhci: MSC lun%u inquiry failed phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        kprint("xhci: MSC lun%u inquiry type=%x removable=%u\n",
               (uint32_t)lun,
               (uint32_t)(inquiry[0] & 0x1fu),
               (uint32_t)((inquiry[1] & 0x80u) != 0u));
        if (!xhci_msc_wait_ready(dev)) {
            kprint("xhci: MSC lun%u not ready sense=%x/%x/%x phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        for (uint32_t i = 0u; i < 10u; i++) {
            if (xhci_msc_read_capacity(dev)) {
                return 1;
            }
            if (xhci_msc_request_sense(dev) && xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_delay_ms(100u);
        }
        kprint("xhci: MSC lun%u read capacity failed sense=%x/%x/%x phase=%u status=%u residue=%x\n",
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

static int xhci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    int result = 0;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_READ_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev, cmd, 10u, out + i * XHCI_SECTOR_SIZE, XHCI_SECTOR_SIZE, 1u)) {
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            (void)xhci_msc_request_sense(dev);
            if (xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            kprint("xhci: MSC read lba=%lx failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
    }
    xhci_end_busy();
    return result;
}

static int xhci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    int result = 0;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_WRITE_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev, cmd, 10u, (void *)(in + i * XHCI_SECTOR_SIZE), XHCI_SECTOR_SIZE, 0u)) {
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            (void)xhci_msc_request_sense(dev);
            if (xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            kprint("xhci: MSC write lba=%lx failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
    }
    xhci_end_busy();
    return result;
}

static void xhci_probe_msc(struct xhci_enum_device *dev) {
    if (g_xhci_msc_count >= XHCI_MAX_ENUM_DEVICES) {
        return;
    }
    xhci_write_msc_name(dev->name, g_xhci_msc_count);
    if (!xhci_alloc_msc_resources(dev)) {
        kprint("xhci: slot%u MSC resource allocation failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_control_set_configuration(dev, dev->configuration)) {
        kprint("xhci: slot%u MSC set config failed\n", (uint32_t)dev->slot_id);
        return;
    }
    xhci_delay_ms(XHCI_MSC_CONFIG_SETTLE_MS);
    if (!xhci_configure_bulk_endpoints(dev)) {
        kprint("xhci: slot%u MSC endpoint config failed in=%u out=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_epid,
               (uint32_t)dev->bulk_out_epid);
        return;
    }
    xhci_delay_ms(50u);
    dev->tag = 0x56780000u;
    if (!xhci_msc_probe_device(dev)) {
        kprint("xhci: slot%u MSC probe failed\n", (uint32_t)dev->slot_id);
        return;
    }
    dev->blockdev.name = dev->name;
    dev->blockdev.block_size = XHCI_SECTOR_SIZE;
    dev->blockdev.block_count = dev->sector_count;
    dev->blockdev.read = xhci_msc_read_impl;
    dev->blockdev.write = xhci_msc_write_impl;
    dev->blockdev.driver_data = dev;
    if (blockdev_register(&dev->blockdev) != 0) {
        kprint("xhci: slot%u MSC block register failed\n", (uint32_t)dev->slot_id);
        return;
    }
    kprint("xhci: slot%u %s sectors=%lx in=%x out=%x\n",
           (uint32_t)dev->slot_id,
           dev->name,
           dev->sector_count,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_out_ep);
    g_xhci_msc_count++;
}

static int xhci_probe_hid_keyboard(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    struct xhci_hid_keyboard *kbd;
    uint32_t kbd_index = XHCI_MAX_HID_KEYBOARDS;

    if (dev == 0 || cfg == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        if (!g_hid_keyboards[i].present) {
            kbd_index = i;
            break;
        }
    }
    if (kbd_index == XHCI_MAX_HID_KEYBOARDS && g_hid_keyboard_count < XHCI_MAX_HID_KEYBOARDS) {
        kbd_index = g_hid_keyboard_count;
        g_hid_keyboard_count++;
    }
    if (kbd_index == XHCI_MAX_HID_KEYBOARDS) {
        kprint("xhci: HID keyboard table full max=%u\n", XHCI_MAX_HID_KEYBOARDS);
        return 0;
    }
    kbd = &g_hid_keyboards[kbd_index];
    memset(kbd, 0, sizeof(*kbd));
    if (!xhci_parse_hid_keyboard_config(dev, kbd, cfg, length)) {
        return 0;
    }
    if (!xhci_control_set_configuration(dev, cfg[5])) {
        kprint("xhci: slot%u HID set config failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    kbd->dev = dev;
    if (kbd->boot_protocol && !xhci_hid_set_protocol(kbd, 0u)) {
        kprint("xhci: slot%u HID set protocol failed\n", (uint32_t)dev->slot_id);
    } else if (!kbd->boot_protocol) {
        kprint("xhci: slot%u HID non-boot fallback iface=%u subclass=%u proto=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interface_subclass,
               (uint32_t)kbd->interface_protocol);
    }
    if (!xhci_hid_set_idle(kbd)) {
        kprint("xhci: slot%u HID set idle failed\n", (uint32_t)dev->slot_id);
    }
    if (!xhci_alloc_hid_keyboard_resources(kbd)) {
        kprint("xhci: slot%u HID resource allocation failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    if (!xhci_configure_hid_interrupt_endpoint(kbd)) {
        kprint("xhci: slot%u HID endpoint config failed epid=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)kbd->interrupt_in_epid);
        return 0;
    }
    (void)xhci_hid_set_leds(kbd, keyboard_led_state());
    kbd->present = 1u;
    (void)xhci_hid_submit_interrupt_report(kbd);
    kprint("xhci: slot%u hidkbd%u iface=%u in=%x epid=%u mps=%u interval=%u report=%u boot=%u\n",
           (uint32_t)dev->slot_id,
           kbd_index,
           (uint32_t)kbd->interface_number,
           (uint32_t)kbd->interrupt_in_ep,
           (uint32_t)kbd->interrupt_in_epid,
           (uint32_t)kbd->interrupt_in_mps,
           (uint32_t)kbd->interrupt_in_interval,
           (uint32_t)kbd->report_size,
           (uint32_t)kbd->boot_protocol);
    return 1;
}

static void xhci_probe_descriptors(struct xhci_enum_device *dev) {
    uint8_t dev_desc[18];
    uint8_t cfg_head[9];
    uint8_t cfg[256];
    uint16_t total_len;

    memset(dev_desc, 0, sizeof(dev_desc));
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, 8u)) {
        kprint("xhci: slot%u device desc8 failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_update_ep0_max_packet(dev, xhci_ep0_max_packet_from_descriptor(dev->speed, dev_desc[7]))) {
        return;
    }
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, sizeof(dev_desc))) {
        kprint("xhci: slot%u device desc18 failed\n", (uint32_t)dev->slot_id);
        return;
    }
    kprint("xhci: slot%u device desc class=%u subclass=%u proto=%u mps0=%u vendor=%x product=%x configs=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev_desc[4],
           (uint32_t)dev_desc[5],
           (uint32_t)dev_desc[6],
           (uint32_t)dev_desc[7],
           (uint32_t)usb_read_u16le(dev_desc + 8),
           (uint32_t)usb_read_u16le(dev_desc + 10),
           (uint32_t)dev_desc[17]);

    memset(cfg_head, 0, sizeof(cfg_head));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg_head, sizeof(cfg_head))) {
        kprint("xhci: slot%u config head failed\n", (uint32_t)dev->slot_id);
        return;
    }
    total_len = usb_read_u16le(cfg_head + 2);
    if (total_len > sizeof(cfg)) {
        total_len = sizeof(cfg);
    }
    memset(cfg, 0, sizeof(cfg));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg, total_len)) {
        return;
    }
    kprint("xhci: slot%u config desc total=%u interfaces=%u config=%u attrs=%x max_power=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)total_len,
           (uint32_t)cfg[4],
           (uint32_t)cfg[5],
           (uint32_t)cfg[7],
           (uint32_t)cfg[8]);
    if (xhci_parse_msc_config(dev, cfg, total_len)) {
        xhci_probe_msc(dev);
        return;
    }
    if (xhci_probe_hid_keyboard(dev, cfg, total_len)) {
        return;
    }
    if (dev_desc[4] == USB_CLASS_HUB || xhci_config_has_hub_interface(cfg, total_len)) {
        xhci_probe_hub(dev, cfg, total_len);
    }
}

static void xhci_reset_port(uint32_t port) {
    uint32_t offset = XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
    uint32_t portsc = xhci_read32(g_xhci.op, offset);

    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) != 0u) {
        return;
    }
    xhci_write32(g_xhci.op, offset, (portsc & XHCI_PORTSC_RW_PRESERVE) | XHCI_PORTSC_PR);
    for (uint32_t i = 0; i < 1000000u; i++) {
        portsc = xhci_read32(g_xhci.op, offset);
        if ((portsc & XHCI_PORTSC_PR) == 0u) {
            break;
        }
    }
}

static int xhci_enumerate_device(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_enum_device(dev) || !xhci_command_enable_slot(&dev->slot_id)) {
        return 0;
    }
    g_xhci.dcbaa[dev->slot_id] = dev->device_context_phys;
    xhci_prepare_address_context(dev);
    if (!xhci_command_address_device(dev)) {
        return 0;
    }
    xhci_delay_ms(XHCI_ADDRESS_SETTLE_MS);
    dev->used = 1u;
    if (dev->parent_slot_id != 0u) {
        kprint("xhci: enumerated hubslot=%u hubport=%u slot=%u rootport=%u route=%x speed=%u\n",
               (uint32_t)dev->parent_slot_id,
               (uint32_t)dev->parent_port,
               (uint32_t)dev->slot_id,
               (uint32_t)dev->port,
               dev->route_string,
               (uint32_t)dev->speed);
    } else {
        kprint("xhci: enumerated port%u slot=%u speed=%u devctx=%x input=%x\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               (uint32_t)dev->speed,
               (uint32_t)dev->device_context_phys,
               (uint32_t)dev->input_context_phys);
    }
    xhci_probe_descriptors(dev);
    return 1;
}

static uint32_t xhci_root_port_offset(uint32_t port) {
    return XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
}

static void xhci_clear_root_port_changes(uint32_t port, uint32_t portsc) {
    uint32_t changes;

    if (port == 0u || port >= XHCI_MAX_ROOT_PORTS) {
        return;
    }
    changes = portsc & XHCI_PORTSC_CHANGE_BITS;
    if (changes == 0u) {
        return;
    }
    xhci_write32(g_xhci.op,
                 xhci_root_port_offset(port),
                 (portsc & XHCI_PORTSC_RW_PRESERVE) | changes);
}

static struct xhci_enum_device *xhci_find_root_port_device(uint8_t controller_index, uint8_t port) {
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == controller_index &&
            dev->parent_slot_id == 0u &&
            dev->port == port) {
            return dev;
        }
    }
    return 0;
}

static struct xhci_enum_device *xhci_find_hub_child(struct xhci_enum_device *hub, uint8_t port) {
    if (hub == 0 || hub->slot_id == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == hub->controller_index &&
            dev->parent_slot_id == hub->slot_id &&
            dev->parent_port == port) {
            return dev;
        }
    }
    return 0;
}

static void xhci_mark_device_detached(struct xhci_enum_device *dev) {
    uint8_t slot_id;

    if (dev == 0 || !dev->used) {
        return;
    }
    slot_id = dev->slot_id;
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (kbd->dev == dev) {
            xhci_hid_release_all_keys(kbd);
            kbd->present = 0u;
            kbd->poll_disabled = 1u;
            kbd->interrupt_pending = 0u;
            kbd->report_queue_head = 0u;
            kbd->report_queue_tail = 0u;
            kbd->report_queue_count = 0u;
        }
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        if (g_enum_devices[i].used &&
            g_enum_devices[i].controller_index == dev->controller_index &&
            g_enum_devices[i].parent_slot_id == slot_id) {
            xhci_mark_device_detached(&g_enum_devices[i]);
        }
    }
    if (dev->parent_slot_id == 0u) {
        g_xhci.root_port_slots[dev->port] = 0u;
    }
    if (slot_id != 0u && slot_id <= g_xhci.max_slots) {
        g_xhci.dcbaa[slot_id] = 0u;
    }
    dev->used = 0u;
    dev->slot_id = 0u;
    dev->hub_port_count = 0u;
}

static int xhci_root_port_is_tracked(uint8_t port) {
    if (g_xhci.root_port_slots[port] != 0u) {
        return 1;
    }
    return xhci_find_root_port_device(g_xhci_active_controller, port) != 0;
}

static int xhci_enumerate_root_port(uint32_t port, const char *reason) {
    uint32_t portsc;
    uint32_t speed;
    struct xhci_enum_device *dev;

    if (port == 0u || port > g_xhci.max_ports || port >= XHCI_MAX_ROOT_PORTS) {
        return 0;
    }
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u || xhci_root_port_is_tracked((uint8_t)port)) {
        return 0;
    }
    xhci_delay_ms(20u);
    xhci_reset_port(port);
    xhci_delay_ms(20u);
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
        kprint("xhci: %s root port%u enable failed portsc=%x\n", reason, port, portsc);
        return 0;
    }
    speed = (portsc >> 10) & 0x0fu;
    kprint("xhci: %s root port%u connected speed=%u enabled=%u powered=%u portsc=%x\n",
           reason,
           port,
           speed,
           (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
           (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
           portsc);
    dev = xhci_alloc_device_record();
    if (dev == 0) {
        kprint("xhci: no enum slots left\n");
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->port = (uint8_t)port;
    dev->controller_index = g_xhci_active_controller;
    dev->speed = (uint8_t)speed;
    if (!xhci_enumerate_device(dev)) {
        return 0;
    }
    g_xhci.root_port_slots[port] = dev->slot_id;
    return 1;
}

static void xhci_enumerate_connected_ports(void) {
    for (uint32_t port = 1; port <= g_xhci.max_ports; port++) {
        (void)xhci_enumerate_root_port(port, "boot");
    }
}

static void xhci_scan_root_hotplug_ports(void) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports && port < XHCI_MAX_ROOT_PORTS; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
        struct xhci_enum_device *dev = xhci_find_root_port_device(g_xhci_active_controller, (uint8_t)port);

        xhci_clear_root_port_changes(port, portsc);
        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
                kprint("xhci: hotplug root port%u disconnected portsc=%x\n", port, portsc);
                xhci_mark_device_detached(dev);
                g_xhci.root_port_slots[port] = 0u;
            }
            continue;
        }
        if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
            continue;
        }
        (void)xhci_enumerate_root_port(port, "hotplug");
    }
}

static void xhci_scan_hub_hotplug(struct xhci_enum_device *hub) {
    if (hub == 0 || !hub->used || hub->hub_port_count == 0u) {
        return;
    }
    for (uint8_t port = 1u; port <= hub->hub_port_count; port++) {
        struct xhci_enum_device *child;
        uint16_t status = 0u;
        uint16_t change = 0u;

        child = xhci_find_hub_child(hub, port);
        if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
            continue;
        }
        xhci_hub_clear_changes(hub, port, change);
        if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
            if (child != 0) {
                kprint("xhci: hotplug hub slot%u port%u disconnected status=%x change=%x\n",
                       (uint32_t)hub->slot_id,
                       (uint32_t)port,
                       (uint32_t)status,
                       (uint32_t)change);
                xhci_mark_device_detached(child);
            }
            continue;
        }
        if (child != 0) {
            continue;
        }
        kprint("xhci: hotplug hub slot%u port%u connected status=%x change=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               (uint32_t)status,
               (uint32_t)change);
        (void)xhci_enumerate_hub_port(hub, port);
    }
}

static void xhci_scan_hub_hotplug_ports(void) {
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == g_xhci_active_controller &&
            dev->hub_port_count != 0u) {
            xhci_scan_hub_hotplug(dev);
        }
    }
}

static void xhci_hotplug_poll(void) {
    uint32_t tick;

    if (g_xhci_controller_count == 0u) {
        return;
    }
    tick = hal_timer_current_ticks();
    if ((uint32_t)(tick - g_xhci_last_hotplug_tick) < XHCI_HOTPLUG_SCAN_TICKS) {
        return;
    }
    g_xhci_last_hotplug_tick = tick;
    for (uint32_t index = 0u; index < g_xhci_controller_count && index < XHCI_MAX_CONTROLLERS; index++) {
        if (!xhci_select_controller((uint8_t)index)) {
            continue;
        }
        xhci_scan_root_hotplug_ports();
        xhci_scan_hub_hotplug_ports();
        xhci_save_active_controller();
    }
}

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

uint32_t xhci_hid_keyboard_count(void) {
    uint32_t count = 0u;

    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        if (g_hid_keyboards[i].present) {
            count++;
        }
    }
    return count;
}

int xhci_poll_keyboard_event(struct keyboard_event *out) {
    uint8_t report[8];
    int result = 0;

    if (out == 0) {
        return 0;
    }
    if (!xhci_try_begin_busy()) {
        return 0;
    }
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
    xhci_hotplug_poll();
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
    if (g_hid_keyboard_count == 0u) {
        goto done;
    }
    for (uint32_t i = 0u; i < g_hid_keyboard_count; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (!kbd->present || kbd->poll_disabled) {
            continue;
        }
        memset(report, 0, sizeof(report));
        if (!xhci_hid_poll_interrupt_report(kbd, report)) {
            continue;
        }
        kbd->report_fail_logged = 0u;
        kbd->poll_fail_count = 0u;
        xhci_hid_process_report(kbd, report);
        if (xhci_hid_pop_event(out)) {
            result = 1;
            goto done;
        }
    }
    xhci_hid_tick_repeats_once();
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
done:
    xhci_end_busy();
    return result;
}
