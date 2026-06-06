#ifndef NEXOS_DRIVERS_USB_XHCI_INTERNAL_H
#define NEXOS_DRIVERS_USB_XHCI_INTERNAL_H

#include "drivers/usb/xhci.h"
#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
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
    XHCI_MSC_BULK_WAIT_SPINS = 3000000000u,
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

    XHCI_RUNTIME_INTR0 = 0x20u,
    XHCI_INTR_IMAN = 0x00u,
    XHCI_INTR_IMOD = 0x04u,
    XHCI_INTR_ERSTSZ = 0x08u,
    XHCI_INTR_ERSTBA = 0x10u,
    XHCI_INTR_ERDP = 0x18u,
    XHCI_EVENT_TRACE = 0u,

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
    USB_PROTO_MOUSE = 0x02u,
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

extern struct xhci_state g_xhci;
extern struct xhci_state g_xhci_controllers[XHCI_MAX_CONTROLLERS];
extern uint32_t g_xhci_controller_count;
extern uint8_t g_xhci_active_controller;
extern struct xhci_enum_device g_enum_devices[XHCI_MAX_ENUM_DEVICES];
extern uint32_t g_xhci_msc_count;
extern struct xhci_hid_keyboard g_hid_keyboards[XHCI_MAX_HID_KEYBOARDS];
extern uint32_t g_hid_keyboard_count;
extern struct xhci_hid_keyboard g_hid_mice[XHCI_MAX_HID_KEYBOARDS];
extern uint32_t g_hid_mouse_count;
extern struct keyboard_event g_hid_event_queue[XHCI_HID_EVENT_QUEUE_SIZE];
extern uint32_t g_hid_event_head;
extern uint32_t g_hid_event_tail;
extern uint32_t g_hid_event_count;
extern uint32_t g_hid_last_repeat_tick;
extern uint32_t g_xhci_last_hotplug_tick;
extern volatile uint32_t g_xhci_busy;


int xhci_try_begin_busy(void);
void xhci_end_busy(void);
void xhci_save_active_controller(void);
int xhci_select_controller(uint8_t index);
uint32_t xhci_read32(volatile uint8_t *base, uint32_t offset);
void xhci_write32(volatile uint8_t *base, uint32_t offset, uint32_t value);
void xhci_write64(volatile uint8_t *base, uint32_t offset, uint64_t value);
int xhci_wait_clear(volatile uint8_t *base, uint32_t offset, uint32_t mask);
int xhci_wait_set(volatile uint8_t *base, uint32_t offset, uint32_t mask);
uint32_t xhci_max_scratchpads(uint32_t hcsparams2);
uint8_t *xhci_context_ptr(uint8_t *base, uint32_t index);
void xhci_ring_trb_set(struct xhci_trb *trb,
                       uint64_t parameter,
                       uint32_t status,
                       uint32_t control);
uint32_t xhci_trb_type(uint32_t control);
void xhci_delay_spin(uint32_t loops);
void xhci_delay_ms(uint32_t ms);
void xhci_bios_handoff(uint32_t hccparams1);
void xhci_log_supported_protocols(uint32_t hccparams1);
void xhci_power_root_ports(void);
void xhci_log_root_ports(uint8_t controller_index, const char *phase);
int xhci_alloc_page(uint64_t *phys_out, void **virt_out);
int xhci_alloc_core_rings(uint32_t scratchpads);
int xhci_alloc_enum_device(struct xhci_enum_device *dev);
int xhci_alloc_msc_resources(struct xhci_enum_device *dev);
int xhci_alloc_hid_keyboard_resources(struct xhci_hid_keyboard *kbd);
struct xhci_enum_device *xhci_alloc_device_record(void);

int xhci_wait_transfer_event_spins(uint8_t slot_id,
                                   uint8_t endpoint_id,
                                   uint32_t *completion_out,
                                   uint64_t expected_trb_phys,
                                   uint32_t max_spins);
int xhci_command_context(uint8_t command_type, struct xhci_enum_device *dev);
uint64_t xhci_transfer_ring_trb(struct xhci_trb *ring,
                                uint64_t ring_phys,
                                uint32_t *enqueue,
                                uint8_t *cycle,
                                uint64_t parameter,
                                uint32_t status,
                                uint32_t control);
int xhci_control_transfer(struct xhci_enum_device *dev,
                          uint8_t request_type,
                          uint8_t request,
                          uint16_t value,
                          uint16_t index,
                          void *data,
                          uint16_t length,
                          uint8_t direction_in);
int xhci_control_set_configuration(struct xhci_enum_device *dev, uint8_t configuration);
int xhci_command_endpoint(uint8_t command_type,
                          struct xhci_enum_device *dev,
                          uint8_t endpoint_id,
                          uint64_t parameter);
int xhci_enumerate_device(struct xhci_enum_device *dev);
int xhci_parse_msc_config(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length);
void xhci_probe_msc(struct xhci_enum_device *dev);
int xhci_msc_command(struct xhci_enum_device *dev,
                     const uint8_t *cmd,
                     uint8_t cmd_len,
                     void *buffer,
                     uint32_t data_len,
                     uint8_t data_in);
int xhci_msc_request_sense(struct xhci_enum_device *dev);
int xhci_msc_medium_not_present(const struct xhci_enum_device *dev);
void xhci_msc_retry_delay(uint8_t failed_phase, uint8_t failed_status);
int xhci_msc_register_blockdev(struct xhci_enum_device *dev);
int xhci_config_has_hub_interface(const uint8_t *cfg, uint32_t length);
void xhci_probe_hub(struct xhci_enum_device *dev, const uint8_t *cfg, uint16_t cfg_len);
void xhci_enumerate_connected_ports(void);
void xhci_hotplug_poll(void);
int xhci_hid_defer_transfer_event(uint8_t slot_id, uint8_t endpoint_id, uint32_t completion);
void xhci_hid_release_all_keys(struct xhci_hid_keyboard *kbd);
int xhci_hid_pop_event(struct keyboard_event *out);
int xhci_hid_poll_interrupt_report(struct xhci_hid_keyboard *kbd, uint8_t report[8]);
void xhci_hid_process_report(struct xhci_hid_keyboard *kbd, const uint8_t report[8]);
void xhci_hid_tick_repeats_once(void);
int xhci_probe_hid_keyboard(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length);
int xhci_probe_hid_mouse(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length);

#endif
