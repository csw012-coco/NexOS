#ifndef NEXOS_DRIVERS_USB_EHCI_INTERNAL_H
#define NEXOS_DRIVERS_USB_EHCI_INTERNAL_H

#include "drivers/usb/ehci.h"

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
    EHCI_MAX_PORTS = 16u,
    EHCI_MAX_MSC = 4u,
    EHCI_MAX_HUBS = 4u,
    EHCI_MAX_HID_KEYBOARDS = 4u,
    EHCI_MAX_HID_MICE = 4u,
    EHCI_HID_EVENT_QUEUE_SIZE = 16u,
    EHCI_HID_REPEAT_DELAY_TICKS = 35u,
    EHCI_HID_REPEAT_RATE_TICKS = 5u,
    EHCI_HOTPLUG_SCAN_TICKS = 25u,
    EHCI_HID_INTERRUPT_POLL_SPINS = 20000u,
    EHCI_MSC_DEBUG = 0u,
    EHCI_PAGE_SIZE = 4096u,
    EHCI_SECTOR_SIZE = 512u,
    EHCI_MSC_READAHEAD_MAX_SECTORS = 1u,
    EHCI_MSC_READAHEAD_INITIAL_SECTORS = 1u,
    EHCI_ASYNC_CONTROL_SPINS = 8000000u,
    EHCI_ASYNC_BULK_META_SPINS = 12000000u,
    EHCI_ASYNC_BULK_DATA_SPINS = 300000000u,
    EHCI_ASYNC_BULK_CSW_SPINS = 12000000u,
    EHCI_MSC_CBW_STAGE_SETTLE_MS = 0u,
    EHCI_MSC_DATA_CSW_SETTLE_MS = 0u,
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
    USB_PROTO_MOUSE = 0x02u,
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
    uint8_t repeat_usage;
    uint8_t repeat_active;
    uint32_t repeat_ticks;
    uint16_t interrupt_in_mps;
    uint8_t last_report[8];
    struct ehci_msc_device xfer;
};

struct ehci_hid_mouse {
    uint8_t present;
    uint8_t address;
    uint8_t configuration;
    uint8_t interface_number;
    uint8_t interrupt_in_ep;
    uint8_t report_fail_logged;
    uint16_t interrupt_in_mps;
    uint8_t last_report[4];
    struct ehci_msc_device xfer;
};

extern struct ehci_regs g_ehci;
extern struct ehci_msc_device g_ehci_msc[EHCI_MAX_MSC];
extern uint32_t g_ehci_msc_count;
extern struct ehci_msc_device g_ehci_hubs[EHCI_MAX_HUBS];
extern uint32_t g_ehci_hub_count;
extern struct ehci_hid_keyboard g_ehci_hid_keyboards[EHCI_MAX_HID_KEYBOARDS];
extern uint32_t g_ehci_hid_keyboard_count;
extern struct ehci_hid_mouse g_ehci_hid_mice[EHCI_MAX_HID_MICE];
extern uint32_t g_ehci_hid_mouse_count;
extern uint8_t g_ehci_next_address;
extern struct keyboard_event g_ehci_hid_event_queue[EHCI_HID_EVENT_QUEUE_SIZE];
extern uint32_t g_ehci_hid_event_head;
extern uint32_t g_ehci_hid_event_tail;
extern uint32_t g_ehci_hid_event_count;
extern uint32_t g_ehci_hid_poll_divider;
extern uint32_t g_ehci_hid_last_repeat_tick;
extern uint32_t g_ehci_last_hotplug_tick;
extern uint64_t g_ehci_async_head_phys;
extern uint64_t g_ehci_async_dummy_qtd_phys;
extern uint64_t g_ehci_periodic_list_phys;
extern struct ehci_qh *g_ehci_async_head;
extern struct ehci_qtd *g_ehci_async_dummy_qtd;
extern uint32_t *g_ehci_periodic_list;

uint8_t ehci_read8(volatile uint8_t *base, uint32_t offset);
void ehci_use_device_controller(const struct ehci_msc_device *dev);
uint32_t ehci_read_cap32(uint32_t offset);
uint32_t ehci_read_op(uint32_t offset);
void ehci_write_op(uint32_t offset, uint32_t value);
void ehci_delay_spin(uint32_t loops);
void ehci_delay_ms(uint32_t ms);
void ehci_dma_barrier(void);
int ehci_wait_op_clear(uint32_t offset, uint32_t mask);
int ehci_wait_op_set(uint32_t offset, uint32_t mask);
int ehci_start_controller(void);
int ehci_reset_controller_runtime(void);
int ehci_ensure_running(void);
uint32_t ehci_port_write_value(uint32_t port);
int ehci_reset_port(uint32_t port_index, uint32_t *port_out);
void ehci_write_name(char *dst, uint32_t index);
int ehci_alloc_async_head(void);
int ehci_alloc_msc_memory(struct ehci_msc_device *dev);
void ehci_qtd_prepare(struct ehci_qtd *qtd,
                      uint64_t phys,
                      uint32_t next_phys,
                      uint32_t pid,
                      uint32_t bytes,
                      uint8_t toggle,
                      uint8_t ioc);
uint32_t ehci_qh_speed_bits(uint8_t speed);
void ehci_qh_prepare(struct ehci_msc_device *dev,
                     uint8_t addr,
                     uint8_t ep,
                     uint16_t mps,
                     uint8_t control,
                     uint32_t first_qtd_phys,
                     uint8_t qtd_controls_toggle,
                     uint8_t initial_toggle);
void ehci_async_head_prepare(struct ehci_msc_device *dev);
int ehci_run_async_transfer(struct ehci_msc_device *dev,
                            uint32_t qtd_count,
                            uint32_t spin_limit);
void ehci_log_transfer_state(const char *label,
                             uint32_t port_index,
                             struct ehci_msc_device *dev,
                             uint32_t qtd_count);
int ehci_control_transfer(struct ehci_msc_device *dev,
                          uint8_t addr,
                          uint16_t mps,
                          const struct usb_ctrl_request *req,
                          void *buffer,
                          uint32_t length,
                          uint8_t data_in);
int ehci_bulk_transfer(struct ehci_msc_device *dev,
                       uint8_t ep_addr,
                       uint16_t mps,
                       uint8_t *toggle,
                       uint64_t phys,
                       uint32_t bytes,
                       uint8_t in,
                       uint32_t *token_out,
                       uint32_t spin_limit);
int ehci_get_descriptor(struct ehci_msc_device *dev,
                        uint8_t addr,
                        uint16_t mps,
                        uint8_t type,
                        uint8_t index,
                        void *buffer,
                        uint16_t length);
int ehci_get_initial_device_descriptor(struct ehci_msc_device *dev,
                                       uint32_t port_index,
                                       uint8_t *buffer,
                                       uint16_t length,
                                       uint16_t mps,
                                       uint32_t *port_out);
int ehci_set_address(struct ehci_msc_device *dev, uint8_t address, uint16_t mps);
int ehci_set_configuration(struct ehci_msc_device *dev, uint8_t configuration, uint16_t mps);
void ehci_log_config_descriptor(const uint8_t *cfg, uint32_t length);

int ehci_parse_msc_config(struct ehci_msc_device *dev, const uint8_t *cfg, uint32_t length);
int ehci_clear_endpoint_halt(struct ehci_msc_device *dev, uint8_t ep);
int ehci_msc_reset_recovery(struct ehci_msc_device *dev);
struct ehci_msc_device *ehci_find_hub_by_addr(uint8_t address);
int ehci_reset_device_port(struct ehci_msc_device *dev);
int ehci_msc_hard_reset_recovery(struct ehci_msc_device *dev);
int ehci_msc_command_recover(struct ehci_msc_device *dev,
                             const uint8_t *cmd,
                             uint8_t cmd_len,
                             void *data,
                             uint32_t data_len,
                             uint8_t data_in);
uint8_t ehci_msc_get_max_lun(struct ehci_msc_device *dev);
void ehci_msc_record_sense(struct ehci_msc_device *dev, const uint8_t *sense, uint32_t length);
void ehci_msc_reduce_readahead(struct ehci_msc_device *dev);
int ehci_msc_request_sense(struct ehci_msc_device *dev, uint8_t *sense, uint32_t length);
int ehci_bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size);
int ehci_msc_buffer_has_transport_signature(const uint8_t *data);
int ehci_msc_sync_cache(struct ehci_msc_device *dev, uint64_t lba, uint32_t count);
void ehci_msc_retry_delay(struct ehci_msc_device *dev);
int ehci_msc_test_unit_ready(struct ehci_msc_device *dev);
int ehci_msc_wait_ready(struct ehci_msc_device *dev);
int ehci_msc_read_capacity(struct ehci_msc_device *dev);
int ehci_msc_probe(struct ehci_msc_device *dev);
int ehci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer);
int ehci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer);

int ehci_parse_hid_keyboard_config(struct ehci_hid_keyboard *kbd, const uint8_t *cfg, uint32_t length);
int ehci_hid_set_protocol(struct ehci_hid_keyboard *kbd, uint8_t protocol);
int ehci_hid_set_idle(struct ehci_hid_keyboard *kbd);
int ehci_hid_get_report(struct ehci_hid_keyboard *kbd, uint8_t report[8]);
void ehci_hid_set_repeat_usage(struct ehci_hid_keyboard *kbd, uint8_t usage);
void ehci_hid_tick_repeat(struct ehci_hid_keyboard *kbd);
void ehci_hid_tick_repeats_once(void);
int ehci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage);
void ehci_hid_queue_event(struct keyboard_event event);
int ehci_hid_pop_event(struct keyboard_event *out);
void ehci_hid_process_report(struct ehci_hid_keyboard *kbd, const uint8_t report[8]);
int ehci_parse_hid_mouse_config(struct ehci_hid_mouse *mouse, const uint8_t *cfg, uint32_t length);
int ehci_hid_mouse_set_protocol(struct ehci_hid_mouse *mouse, uint8_t protocol);
int ehci_hid_mouse_set_idle(struct ehci_hid_mouse *mouse);
int ehci_hid_mouse_get_report(struct ehci_hid_mouse *mouse, uint8_t report[4]);
int ehci_hid_mouse_poll_interrupt_report(struct ehci_hid_mouse *mouse, uint8_t report[4]);
void ehci_hid_mouse_process_report(struct ehci_hid_mouse *mouse, const uint8_t report[4], uint32_t tick);

int ehci_config_has_hub_interface(const uint8_t *cfg, uint32_t length);
struct ehci_msc_device *ehci_select_probe_xfer(void);
void ehci_log_device_summary(struct ehci_msc_device *dev,
                             uint32_t root_port,
                             const uint8_t *dev_desc,
                             uint16_t mps0);
void ehci_log_unsupported(struct ehci_msc_device *dev,
                          uint32_t root_port,
                          uint16_t total_len,
                          const uint8_t *dev_desc);
int ehci_hub_get_descriptor(struct ehci_msc_device *hub, uint8_t *buffer, uint16_t length);
int ehci_hub_set_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature);
int ehci_hub_clear_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature);
int ehci_hub_get_port_status(struct ehci_msc_device *hub,
                             uint8_t port,
                             uint16_t *status_out,
                             uint16_t *change_out);
uint8_t ehci_hub_child_speed(uint16_t status);
void ehci_hub_clear_changes(struct ehci_msc_device *hub, uint8_t port, uint16_t change);
int ehci_enumerate_hub_port(struct ehci_msc_device *hub, uint32_t root_port, uint8_t port);
int ehci_probe_hub(struct ehci_msc_device *dev,
                   uint32_t root_port,
                   uint16_t mps0,
                   const uint8_t *cfg,
                   uint32_t cfg_len);
int ehci_finish_device_enumeration(struct ehci_msc_device *dev,
                                   uint32_t root_port,
                                   uint8_t *dev_desc,
                                   uint16_t mps0);
int ehci_enumerate_port(uint32_t port_index);
void ehci_hotplug_poll(void);

#endif
