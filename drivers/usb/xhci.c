#include "drivers/usb/xhci.h"

#include "block/blockdev.h"
#include "drivers/bus/pci.h"
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
    XHCI_MAX_ENUM_DEVICES = 8u,
    XHCI_MAX_HUB_PORTS = 15u,

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
    XHCI_PORTSC_PP = 1u << 9
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
    XHCI_TRB_TRANSFER_EVENT = 32u,
    XHCI_TRB_COMMAND_COMPLETION = 33u,

    XHCI_CC_SUCCESS = 1u,

    XHCI_SPEED_FULL = 1u,
    XHCI_SPEED_LOW = 2u,
    XHCI_SPEED_HIGH = 3u,
    XHCI_SPEED_SUPER = 4u,

    XHCI_SLOT_FLAG = 1u << 0,
    XHCI_EP0_FLAG = 1u << 1,
    XHCI_SLOT_HUB = 1u << 26,
    XHCI_SLOT_LAST_CTX_1 = 1u << 27,

    USB_REQ_GET_STATUS = 0x00u,
    USB_REQ_CLEAR_FEATURE = 0x01u,
    USB_REQ_SET_FEATURE = 0x03u,
    USB_REQ_GET_DESCRIPTOR = 0x06u,
    USB_REQ_SET_CONFIGURATION = 0x09u,
    USB_DESC_DEVICE = 0x01u,
    USB_DESC_CONFIGURATION = 0x02u,
    USB_DESC_HUB = 0x29u,
    USB_DESC_SS_HUB = 0x2au,
    USB_CLASS_HUB = 0x09u,
    USB_CLASS_MASS_STORAGE = 0x08u,
    USB_SUBCLASS_SCSI = 0x06u,
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

    MSC_CBW_SIGNATURE = 0x43425355u,
    MSC_CSW_SIGNATURE = 0x53425355u,
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
};

struct xhci_enum_device {
    char name[12];
    uint8_t used;
    uint8_t slot_id;
    uint8_t port;
    uint8_t speed;
    uint8_t route_depth;
    uint8_t parent_slot_id;
    uint8_t parent_port;
    uint8_t hub_port_count;
    uint8_t configuration;
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

static struct xhci_state g_xhci;
static struct xhci_enum_device g_enum_devices[XHCI_MAX_ENUM_DEVICES];
static uint32_t g_xhci_msc_count;

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

static struct xhci_enum_device *xhci_alloc_device_record(void) {
    for (uint32_t i = 0; i < XHCI_MAX_ENUM_DEVICES; i++) {
        if (!g_enum_devices[i].used && g_enum_devices[i].slot_id == 0u) {
            return &g_enum_devices[i];
        }
    }
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
                 g_xhci.event_ring_phys + g_xhci.event_dequeue * XHCI_TRB_SIZE);
    return 1;
}

static int xhci_wait_command_completion(uint8_t *slot_id_out, uint32_t *completion_out) {
    for (uint32_t spin = 0; spin < 5000000u; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
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

static int xhci_wait_transfer_event(uint8_t slot_id, uint8_t endpoint_id, uint32_t *completion_out) {
    for (uint32_t spin = 0; spin < 5000000u; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
        if (xhci_trb_type(control) != XHCI_TRB_TRANSFER_EVENT) {
            continue;
        }
        if (((control >> 24) & 0xffu) != slot_id ||
            ((control >> 16) & 0x1fu) != endpoint_id) {
            continue;
        }
        if (completion_out != 0) {
            *completion_out = (event.status >> 24) & 0xffu;
        }
        return 1;
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
    if (speed == XHCI_SPEED_LOW) {
        return 8u;
    }
    if (speed >= XHCI_SPEED_SUPER) {
        return 512u;
    }
    return 64u;
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
    ep0_ctx[1] = (4u << 3) | (max_packet << 16);
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

static void xhci_transfer_ring_trb(struct xhci_trb *ring,
                                   uint64_t ring_phys,
                                   uint32_t *enqueue,
                                   uint8_t *cycle,
                                   uint64_t parameter,
                                   uint32_t status,
                                   uint32_t control) {
    struct xhci_trb *trb = &ring[*enqueue];

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

    if (dev == 0 || length > XHCI_PAGE_SIZE || (length != 0u && buffer == 0)) {
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
    if (!xhci_wait_transfer_event(dev->slot_id, 1u, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: control failed port=%u slot=%u req=%u type=%x cc=%u\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               (uint32_t)request,
               (uint32_t)request_type,
               completion);
        return 0;
    }
    if (buffer != 0 && length != 0u && data_in) {
        memcpy(buffer, dev->descriptor, length);
    }
    return 1;
}

static int xhci_control_get_descriptor(struct xhci_enum_device *dev,
                                       uint8_t desc_type,
                                       uint8_t desc_index,
                                       void *buffer,
                                       uint16_t length) {
    return xhci_control_transfer(dev,
                                 0x80u,
                                 USB_REQ_GET_DESCRIPTOR,
                                 (uint16_t)(((uint16_t)desc_type << 8) | desc_index),
                                 0u,
                                 buffer,
                                 length,
                                 1u);
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

static uint16_t xhci_read_u16le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t xhci_read_u32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void xhci_write_u32le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void xhci_write_u32be(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
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

    if (dev == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
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
        } else if (type == 5u && len >= 7u && in_msc) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = xhci_read_u16le(cfg + offset + 4u);
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
    return dev->configuration != 0u &&
           dev->bulk_in_epid != 0u &&
           dev->bulk_out_epid != 0u &&
           dev->bulk_in_mps != 0u &&
           dev->bulk_out_mps != 0u;
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
        *status_out = xhci_read_u16le(data);
    }
    if (change_out != 0) {
        *change_out = xhci_read_u16le(data + 2);
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

    out_ctx[1] = (2u << 3) | ((uint32_t)dev->bulk_out_mps << 16);
    out_ctx[2] = (uint32_t)dev->bulk_out_ring_phys | 1u;
    out_ctx[3] = (uint32_t)(dev->bulk_out_ring_phys >> 32);
    out_ctx[4] = dev->bulk_out_mps;

    in_ctx[1] = (6u << 3) | ((uint32_t)dev->bulk_in_mps << 16);
    in_ctx[2] = (uint32_t)dev->bulk_in_ring_phys | 1u;
    in_ctx[3] = (uint32_t)(dev->bulk_in_ring_phys >> 32);
    in_ctx[4] = dev->bulk_in_mps;
}

static int xhci_configure_bulk_endpoints(struct xhci_enum_device *dev) {
    xhci_prepare_bulk_context(dev);
    return xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, dev);
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
    uint32_t *enqueue;
    uint8_t *cycle;

    if (dev == 0 || endpoint_id == 0u || bytes > XHCI_PAGE_SIZE) {
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
    xhci_transfer_ring_trb(ring,
                           ring_phys,
                           enqueue,
                           cycle,
                           phys,
                           bytes,
                           (XHCI_TRB_NORMAL << 10) | (1u << 5));
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, endpoint_id);
    return xhci_wait_transfer_event(dev->slot_id, endpoint_id, &completion) &&
           completion == XHCI_CC_SUCCESS;
}

static int xhci_msc_command(struct xhci_enum_device *dev,
                            const uint8_t *cmd,
                            uint8_t cmd_len,
                            void *buffer,
                            uint32_t data_len,
                            uint8_t data_in) {
    uint32_t tag;

    if (dev == 0 || cmd == 0 || cmd_len > 16u || data_len > XHCI_PAGE_SIZE) {
        return 0;
    }
    memset(dev->cbw, 0, 31u);
    memset(dev->csw, 0, 13u);
    tag = ++dev->tag;
    xhci_write_u32le(dev->cbw + 0, MSC_CBW_SIGNATURE);
    xhci_write_u32le(dev->cbw + 4, tag);
    xhci_write_u32le(dev->cbw + 8, data_len);
    dev->cbw[12] = data_in ? 0x80u : 0u;
    dev->cbw[13] = 0u;
    dev->cbw[14] = cmd_len;
    memcpy(dev->cbw + 15, cmd, cmd_len);
    if (buffer != 0 && data_len != 0u && !data_in) {
        memcpy(dev->data, buffer, data_len);
    }
    if (!xhci_bulk_transfer(dev, dev->bulk_out_epid, dev->cbw_phys, 31u)) {
        return 0;
    }
    if (data_len != 0u) {
        if (!xhci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_epid : dev->bulk_out_epid,
                                dev->data_phys,
                                data_len)) {
            return 0;
        }
        if (buffer != 0 && data_in) {
            memcpy(buffer, dev->data, data_len);
        }
    }
    if (!xhci_bulk_transfer(dev, dev->bulk_in_epid, dev->csw_phys, 13u)) {
        return 0;
    }
    return xhci_read_u16le(dev->csw) == (uint16_t)MSC_CSW_SIGNATURE &&
           xhci_read_u16le(dev->csw + 2) == (uint16_t)(MSC_CSW_SIGNATURE >> 16) &&
           dev->csw[12] == 0u;
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
    last_lba = xhci_read_u32be(cap);
    block_len = xhci_read_u32be(cap + 4);
    if (block_len != XHCI_SECTOR_SIZE) {
        return 0;
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return 1;
}

static int xhci_msc_probe_device(struct xhci_enum_device *dev) {
    uint8_t cmd[16];
    uint8_t inquiry[36];

    memset(cmd, 0, sizeof(cmd));
    memset(inquiry, 0, sizeof(inquiry));
    cmd[0] = SCSI_INQUIRY;
    cmd[4] = sizeof(inquiry);
    if (!xhci_msc_command(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
        return 0;
    }
    return xhci_msc_read_capacity(dev);
}

static int xhci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;

    if (dev == 0 || buffer == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_READ_10;
        xhci_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        if (!xhci_msc_command(dev, cmd, 10u, out + i * XHCI_SECTOR_SIZE, XHCI_SECTOR_SIZE, 1u)) {
            return -1;
        }
    }
    return 0;
}

static int xhci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;

    if (dev == 0 || buffer == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_WRITE_10;
        xhci_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        if (!xhci_msc_command(dev, cmd, 10u, (void *)(in + i * XHCI_SECTOR_SIZE), XHCI_SECTOR_SIZE, 0u)) {
            return -1;
        }
    }
    return 0;
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
    if (!xhci_configure_bulk_endpoints(dev)) {
        kprint("xhci: slot%u MSC endpoint config failed in=%u out=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_epid,
               (uint32_t)dev->bulk_out_epid);
        return;
    }
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

static void xhci_probe_descriptors(struct xhci_enum_device *dev) {
    uint8_t dev_desc[18];
    uint8_t cfg_head[9];
    uint8_t cfg[256];
    uint16_t total_len;

    memset(dev_desc, 0, sizeof(dev_desc));
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, sizeof(dev_desc))) {
        return;
    }
    kprint("xhci: slot%u device desc class=%u subclass=%u proto=%u mps0=%u vendor=%x product=%x configs=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev_desc[4],
           (uint32_t)dev_desc[5],
           (uint32_t)dev_desc[6],
           (uint32_t)dev_desc[7],
           (uint32_t)xhci_read_u16le(dev_desc + 8),
           (uint32_t)xhci_read_u16le(dev_desc + 10),
           (uint32_t)dev_desc[17]);

    memset(cfg_head, 0, sizeof(cfg_head));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg_head, sizeof(cfg_head))) {
        return;
    }
    total_len = xhci_read_u16le(cfg_head + 2);
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
    xhci_write32(g_xhci.op, offset, (portsc & 0x0e00c3e0u) | XHCI_PORTSC_PR);
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

static void xhci_enumerate_connected_ports(void) {
    for (uint32_t port = 1; port <= g_xhci.max_ports; port++) {
        uint32_t portsc;
        uint32_t speed;
        struct xhci_enum_device *dev;

        xhci_reset_port(port);
        portsc = xhci_read32(g_xhci.op, XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE);
        if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
            continue;
        }
        speed = (portsc >> 10) & 0x0fu;
        dev = xhci_alloc_device_record();
        if (dev == 0) {
            kprint("xhci: no enum slots left\n");
            return;
        }
        memset(dev, 0, sizeof(*dev));
        dev->port = (uint8_t)port;
        dev->speed = (uint8_t)speed;
        (void)xhci_enumerate_device(dev);
    }
}

void xhci_init(void) {
    struct pci_xhci_controller xhci;
    uint64_t mmio;
    uint32_t hcs1;
    uint32_t hcs2;
    uint32_t hcc1;
    uint32_t dboff;
    uint32_t rtsoff;
    volatile uint8_t *intr0;
    uint32_t scratchpads;

    memset(&g_xhci, 0, sizeof(g_xhci));
    g_xhci_msc_count = 0u;
    if (!pci_find_xhci_controller(&xhci)) {
        return;
    }

    pci_config_write16(xhci.bus, xhci.slot, xhci.function, 0x04,
                       (uint16_t)(pci_config_read16(xhci.bus, xhci.slot, xhci.function, 0x04) | 0x0006u));
    mmio = ((uint64_t)(xhci.mmio_base_hi & 0xffffffffu) << 32) | (uint64_t)(xhci.mmio_base_lo & 0xfffffff0u);
    if (mmio == 0u || mmio > 0xffffffffull) {
        kprint("xhci: unsupported mmio=%lx\n", mmio);
        return;
    }
    g_xhci.cap = (volatile uint8_t *)hal_phys_direct_map(mmio);
    if (g_xhci.cap == 0) {
        return;
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

    xhci_write32(g_xhci.op, XHCI_OP_USBCMD, xhci_read32(g_xhci.op, XHCI_OP_USBCMD) & ~XHCI_USBCMD_RS);
    (void)xhci_wait_set(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH);
    xhci_write32(g_xhci.op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST);
    if (!xhci_wait_clear(g_xhci.op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST) ||
        !xhci_wait_clear(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_CNR)) {
        kprint("xhci: reset timeout\n");
        return;
    }
    if (!xhci_alloc_core_rings(scratchpads)) {
        kprint("xhci: dma allocation failed\n");
        return;
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
        return;
    }

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
    kprint("xhci: controller bdf=%u:%u.%u mmio=%x slots=%u intrs=%u ports=%u ctx=%u scratch=%u hcc=%x rev=msc-v2\n",
           (uint32_t)xhci.bus,
           (uint32_t)xhci.slot,
           (uint32_t)xhci.function,
           (uint32_t)mmio,
           (uint32_t)g_xhci.max_slots,
           (uint32_t)g_xhci.max_intrs,
           (uint32_t)g_xhci.max_ports,
           (uint32_t)g_xhci.context_size,
           scratchpads,
           hcc1);
}

uint32_t xhci_port_count(void) {
    return g_xhci.initialized ? g_xhci.max_ports : 0u;
}

uint32_t xhci_connected_port_count(void) {
    return g_xhci.connected_ports;
}
