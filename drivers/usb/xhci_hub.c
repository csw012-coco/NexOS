#include "drivers/usb/xhci_internal.h"

enum {
    XHCI_ROOT_ENUM_RETRY_MS = 5000u,
    XHCI_HUB_ENUM_RETRY_MS = 5000u,
    XHCI_COMMAND_BACKOFF_ENUM_RETRY_MS = 30000u,
    XHCI_DISCONNECT_CONFIRM_MS = 100u
};

static uint32_t g_xhci_root_port_retry_after[XHCI_MAX_CONTROLLERS][XHCI_MAX_ROOT_PORTS];
static uint32_t g_xhci_root_port_failure_sig[XHCI_MAX_CONTROLLERS][XHCI_MAX_ROOT_PORTS];
static uint8_t g_xhci_root_port_failure_latched[XHCI_MAX_CONTROLLERS][XHCI_MAX_ROOT_PORTS];
static uint8_t g_xhci_root_port_disconnect_pending[XHCI_MAX_CONTROLLERS][XHCI_MAX_ROOT_PORTS];

static uint8_t xhci_hub_child_speed(struct xhci_enum_device *hub, uint16_t status);
static void xhci_mark_device_detached(struct xhci_enum_device *dev);
static void xhci_discard_failed_device(struct xhci_enum_device *dev);
static void xhci_rebind_fresh_msc_to_rootfs_if_needed(struct xhci_enum_device *fresh,
                                                      const char *reason);

static uint32_t xhci_hotplug_now_ticks(void) {
    return hal_timer_current_ticks();
}

static uint32_t xhci_hotplug_ms_to_ticks(uint32_t ms) {
    uint32_t hz = hal_timer_hz();
    uint64_t ticks;

    if (hz == 0u) {
        hz = 1000u;
    }
    ticks = ((uint64_t)hz * (uint64_t)ms + 999u) / 1000u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    if (ticks > 0xffffffffull) {
        ticks = 0xffffffffu;
    }
    return (uint32_t)ticks;
}

static uint32_t xhci_root_retry_delay_ticks(void) {
    return xhci_hotplug_ms_to_ticks(XHCI_ROOT_ENUM_RETRY_MS);
}

static uint32_t xhci_hub_retry_delay_ticks(void) {
    return xhci_hotplug_ms_to_ticks(XHCI_HUB_ENUM_RETRY_MS);
}

static uint32_t xhci_command_backoff_retry_delay_ticks(void) {
    return xhci_hotplug_ms_to_ticks(XHCI_COMMAND_BACKOFF_ENUM_RETRY_MS);
}

static uint32_t xhci_disconnect_confirm_delay_ticks(void) {
    return xhci_hotplug_ms_to_ticks(XHCI_DISCONNECT_CONFIRM_MS);
}

static int xhci_retry_ready(uint32_t now, uint32_t retry_after) {
    return retry_after == 0u || (int32_t)(now - retry_after) >= 0;
}

static int xhci_reason_equals(const char *reason, const char *expected) {
    uint32_t i = 0u;

    if (reason == 0 || expected == 0) {
        return 0;
    }
    while (reason[i] != '\0' && expected[i] != '\0') {
        if (reason[i] != expected[i]) {
            return 0;
        }
        i++;
    }
    return reason[i] == '\0' && expected[i] == '\0';
}

static int xhci_reason_is_command_backoff(const char *reason) {
    return xhci_reason_equals(reason, "command-backoff");
}

static int xhci_enum_failure_is_observable(const char *reason) {
    return xhci_reason_equals(reason, "unsupported-class");
}

static uint32_t xhci_enum_failure_retry_delay_ticks(const char *reason) {
    if (xhci_command_backoff_active() ||
        xhci_reason_is_command_backoff(reason)) {
        return xhci_command_backoff_retry_delay_ticks();
    }
    return xhci_root_retry_delay_ticks();
}

static uint32_t xhci_hub_enum_failure_retry_delay_ticks(const char *reason) {
    if (xhci_command_backoff_active() ||
        xhci_reason_is_command_backoff(reason)) {
        return xhci_command_backoff_retry_delay_ticks();
    }
    return xhci_hub_retry_delay_ticks();
}

static uint32_t xhci_root_port_failure_signature(uint32_t portsc) {
    return portsc & (XHCI_PORTSC_CCS |
                     XHCI_PORTSC_PED |
                     XHCI_PORTSC_PP |
                     (0x0fu << 10));
}

static int xhci_root_port_failure_index_valid(uint32_t port) {
    return g_xhci_active_controller < XHCI_MAX_CONTROLLERS &&
           port != 0u &&
           port < XHCI_MAX_ROOT_PORTS;
}

static void xhci_root_port_clear_failure(uint32_t port) {
    if (!xhci_root_port_failure_index_valid(port)) {
        return;
    }
    g_xhci_root_port_failure_latched[g_xhci_active_controller][port] = 0u;
    g_xhci_root_port_failure_sig[g_xhci_active_controller][port] = 0u;
}

static int xhci_root_port_failure_latched(uint32_t port, uint32_t portsc) {
    uint32_t sig;

    if (!xhci_root_port_failure_index_valid(port)) {
        return 0;
    }
    if (g_xhci_root_port_failure_latched[g_xhci_active_controller][port] == 0u) {
        return 0;
    }
    sig = xhci_root_port_failure_signature(portsc);
    return g_xhci_root_port_failure_sig[g_xhci_active_controller][port] == sig;
}

static void xhci_root_port_latch_failure(uint32_t port, uint32_t portsc) {
    if (!xhci_root_port_failure_index_valid(port)) {
        return;
    }
    g_xhci_root_port_failure_sig[g_xhci_active_controller][port] =
        xhci_root_port_failure_signature(portsc);
    g_xhci_root_port_failure_latched[g_xhci_active_controller][port] = 1u;
}

static uint32_t xhci_hub_port_failure_signature(uint16_t status) {
    return (uint32_t)(status & (USB_HUB_PORT_CONNECTION |
                                USB_HUB_PORT_ENABLE |
                                USB_HUB_PORT_RESET |
                                USB_HUB_PORT_POWER |
                                USB_HUB_PORT_LOW_SPEED |
                                USB_HUB_PORT_HIGH_SPEED));
}

static int xhci_hub_port_failure_index_valid(struct xhci_enum_device *hub, uint8_t port) {
    return hub != 0 &&
           port != 0u &&
           port <= XHCI_MAX_HUB_PORTS;
}

static void xhci_hub_port_clear_failure(struct xhci_enum_device *hub, uint8_t port) {
    if (!xhci_hub_port_failure_index_valid(hub, port)) {
        return;
    }
    hub->hub_port_failure_latched[port] = 0u;
    hub->hub_port_failure_sig[port] = 0u;
}

static int xhci_hub_port_failure_latched(struct xhci_enum_device *hub,
                                         uint8_t port,
                                         uint16_t status) {
    if (!xhci_hub_port_failure_index_valid(hub, port)) {
        return 0;
    }
    if (hub->hub_port_failure_latched[port] == 0u) {
        return 0;
    }
    return hub->hub_port_failure_sig[port] == xhci_hub_port_failure_signature(status);
}

static void xhci_hub_port_latch_failure(struct xhci_enum_device *hub,
                                        uint8_t port,
                                        uint16_t status) {
    if (!xhci_hub_port_failure_index_valid(hub, port)) {
        return;
    }
    hub->hub_port_failure_sig[port] = xhci_hub_port_failure_signature(status);
    hub->hub_port_failure_latched[port] = 1u;
}

int xhci_config_has_hub_interface(const uint8_t *cfg, uint32_t length) {
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

static int xhci_hub_set_depth(struct xhci_enum_device *hub, uint8_t depth) {
    return xhci_control_transfer(hub,
                                 0x20u,
                                 USB_REQ_SET_HUB_DEPTH,
                                 depth,
                                 0u,
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

static void xhci_log_hub_port_status(const char *phase,
                                     struct xhci_enum_device *hub,
                                     uint8_t port,
                                     uint16_t status,
                                     uint16_t change) {
    uint32_t power = hub != 0 && hub->speed >= XHCI_SPEED_SUPER
        ? (uint32_t)((status & USB_SS_HUB_PORT_POWER) != 0u)
        : (uint32_t)((status & USB_HUB_PORT_POWER) != 0u);
    uint32_t link_state = hub != 0 && hub->speed >= XHCI_SPEED_SUPER
        ? (uint32_t)(status & USB_SS_HUB_PORT_LINK_STATE)
        : 0u;

    kprint("xhci: %s hub slot%u rootport=%u port%u status=%x change=%x conn=%u en=%u reset=%u pwr=%u link=%x speed=%u\n",
           phase != 0 ? phase : "port",
           hub != 0 ? (uint32_t)hub->slot_id : 0u,
           hub != 0 ? (uint32_t)hub->port : 0u,
           (uint32_t)port,
           (uint32_t)status,
           (uint32_t)change,
           (uint32_t)((status & USB_HUB_PORT_CONNECTION) != 0u),
           (uint32_t)((status & USB_HUB_PORT_ENABLE) != 0u),
           (uint32_t)((status & USB_HUB_PORT_RESET) != 0u),
           power,
           link_state,
           (uint32_t)xhci_hub_child_speed(hub, status));
}

static uint8_t xhci_hub_child_speed(struct xhci_enum_device *hub,
                                    uint16_t status) {
    if (hub->speed >= XHCI_SPEED_SUPER) {
        return XHCI_SPEED_SUPER;
    }

    if ((status & USB_HUB_PORT_LOW_SPEED) != 0u) {
        return XHCI_SPEED_LOW;
    }

    if ((status & USB_HUB_PORT_HIGH_SPEED) != 0u) {
        return XHCI_SPEED_HIGH;
    }

    return XHCI_SPEED_FULL;
}

static int xhci_hub_speed_uses_tt(uint8_t speed) {
    return speed == XHCI_SPEED_FULL || speed == XHCI_SPEED_LOW;
}

static int xhci_hub_port_uses_reset(struct xhci_enum_device *hub) {
    return hub != 0 && hub->speed < XHCI_SPEED_SUPER;
}

static void xhci_hub_clear_changes(struct xhci_enum_device *hub, uint8_t port, uint16_t change) {
    if (change != 0u) {
        kprint("xhci: hub slot%u port%u clear change=%x\n",
               hub != 0 ? (uint32_t)hub->slot_id : 0u,
               (uint32_t)port,
               (uint32_t)change);
    }
    if ((change & USB_HUB_PORT_CONNECTION) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION);
    }
    if ((change & USB_HUB_PORT_ENABLE) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_ENABLE);
    }
    if ((change & USB_HUB_PORT_RESET) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_RESET);
    }
    if (hub != 0 && hub->speed >= XHCI_SPEED_SUPER) {
        if ((change & USB_SS_HUB_CHANGE_BH_RESET) != 0u) {
            (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_BH_PORT_RESET);
        }
        if ((change & USB_SS_HUB_CHANGE_LINK_STATE) != 0u) {
            (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_LINK_STATE);
        }
        if ((change & USB_SS_HUB_CHANGE_CONFIG_ERROR) != 0u) {
            (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONFIG_ERROR);
        }
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
    slot_ctx[2] = (slot_ctx[2] & ~(3u << 16)) |
                  ((uint32_t)(hub->hub_tt_think_time & 3u) << 16);
}

static int xhci_update_hub_context(struct xhci_enum_device *hub,
                                   uint8_t port_count) {
    uint8_t *out_slot;
    uint32_t *slot;
    int ok;

    xhci_prepare_hub_context(hub, port_count);
    ok = xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, hub);
    if (!ok) {
        xhci_prepare_hub_context(hub, port_count);
        ok = xhci_command_context(XHCI_TRB_EVALUATE_CONTEXT, hub);
        if (!ok) {
            return 0;
        }
    }

    out_slot = xhci_context_ptr(hub->device_context, 0u);
    slot = (uint32_t *)out_slot;

    kprint("xhci: HUBCTX slot=%u out0=%x out1=%x out2=%x out3=%x ports=%u\n",
           (uint32_t)hub->slot_id,
           slot[0],
           slot[1],
           slot[2],
           slot[3],
           (uint32_t)port_count);

    return (slot[0] & XHCI_SLOT_HUB) != 0u &&
           ((slot[1] >> 24) & 0xffu) == port_count;
}

static int xhci_enumerate_hub_port(struct xhci_enum_device *hub, uint8_t port) {
    struct xhci_enum_device *child;
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint32_t now;

    if (hub->route_depth >= 5u || port == 0u || port > 15u) {
        return 0;
    }
    now = xhci_hotplug_now_ticks();
    if (!xhci_retry_ready(now, hub->hub_port_retry_after[port])) {
        return 0;
    }
    if (xhci_command_backoff_active()) {
        hub->hub_port_retry_after[port] = now + xhci_command_backoff_retry_delay_ticks();
        return 0;
    }
    if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
        kprint("xhci: hub slot%u port%u status failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
        hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
        return 0;
    }
    if (change != 0u || (status & USB_HUB_PORT_CONNECTION) != 0u) {
        xhci_log_hub_port_status("enum-pre", hub, port, status, change);
    }
    if (change != 0u) {
        xhci_hub_port_clear_failure(hub, port);
        hub->hub_port_retry_after[port] = 0u;
    }
    xhci_hub_clear_changes(hub, port, change);
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        xhci_hub_port_clear_failure(hub, port);
        hub->hub_port_retry_after[port] = 0u;
        return 0;
    }
    if (xhci_hub_port_failure_latched(hub, port, status)) {
        return 0;
    }
    XHCI_HUB_TRACE("xhci: hub slot%u port%u connected status=%x change=%x\n",
                   (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
    if (xhci_hub_port_uses_reset(hub)) {
        if (!xhci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
            kprint("xhci: hub slot%u port%u reset failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
            xhci_hub_port_latch_failure(hub, port, status);
            hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
            return 0;
        }
        kprint("xhci: hub slot%u port%u reset start status=%x change=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               (uint32_t)status,
               (uint32_t)change);
        xhci_delay_ms(80u);
        for (uint32_t i = 0; i < 200u; i++) {
            if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
                kprint("xhci: hub slot%u port%u reset status read failed\n",
                       (uint32_t)hub->slot_id,
                       (uint32_t)port);
                xhci_hub_port_latch_failure(hub, port, status);
                hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
                return 0;
            }
            if ((status & USB_HUB_PORT_RESET) == 0u) {
                break;
            }
            xhci_delay_ms(1u);
        }
    } else {
        kprint("xhci: hub slot%u port%u reset skip ss status=%x change=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               (uint32_t)status,
               (uint32_t)change);
        xhci_delay_ms(150u);
    }
    xhci_log_hub_port_status("reset-done", hub, port, status, change);
    xhci_hub_clear_changes(hub, port, change);
    if (!xhci_hub_get_port_status(hub, port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("xhci: hub slot%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
        xhci_hub_port_latch_failure(hub, port, status);
        hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
        return 0;
    }
    xhci_log_hub_port_status("enabled", hub, port, status, change);
    xhci_delay_ms(50u);

    child = xhci_alloc_device_record();
    if (child == 0) {
        kprint("xhci: hub slot%u port%u no device slots\n", (uint32_t)hub->slot_id, (uint32_t)port);
        xhci_hub_port_latch_failure(hub, port, status);
        hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
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
    if (xhci_hub_speed_uses_tt(child->speed)) {
        if (hub->speed == XHCI_SPEED_HIGH) {
            child->tt_hub_slot_id = hub->slot_id;
            child->tt_port = port;
            child->tt_think_time = hub->hub_tt_think_time;
        } else if (hub->tt_hub_slot_id != 0u) {
            child->tt_hub_slot_id = hub->tt_hub_slot_id;
            child->tt_port = hub->tt_port;
            child->tt_think_time = hub->tt_think_time;
        }
    }
    XHCI_HUB_TRACE("xhci: hub slot%u port%u route=%x speed=%u tt=%u/%u/%u\n",
                   (uint32_t)hub->slot_id,
                   (uint32_t)port,
                   child->route_string,
                   (uint32_t)child->speed,
                   (uint32_t)child->tt_hub_slot_id,
                   (uint32_t)child->tt_port,
                   (uint32_t)child->tt_think_time);
    if (!xhci_enumerate_device(child)) {
        const char *failure = xhci_enum_failure_reason(child);

        if (child->speed >= XHCI_SPEED_SUPER &&
            xhci_reason_equals(failure, "address-device")) {
            kprint("xhci: hub slot%u port%u retry ss address status=%x change=%x route=%x\n",
                   (uint32_t)hub->slot_id,
                   (uint32_t)port,
                   (uint32_t)status,
                   (uint32_t)change,
                   child->route_string);
            xhci_delay_ms(250u);
            if (xhci_hub_get_port_status(hub, port, &status, &change)) {
                xhci_log_hub_port_status("retry-pre", hub, port, status, change);
                xhci_hub_clear_changes(hub, port, change);
                if ((status & USB_HUB_PORT_CONNECTION) != 0u &&
                    (status & USB_HUB_PORT_ENABLE) != 0u &&
                    xhci_enumerate_device(child)) {
                    goto hub_child_enumerated;
                }
            }
            failure = xhci_enum_failure_reason(child);
        }

        kprint("xhci: hub slot%u port%u enumerate failed reason=%s status=%x change=%x speed=%u route=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               failure,
               (uint32_t)status,
               (uint32_t)change,
               (uint32_t)child->speed,
               child->route_string);
        xhci_hub_port_latch_failure(hub, port, status);
        hub->hub_port_retry_after[port] = now + xhci_hub_enum_failure_retry_delay_ticks(failure);
        xhci_discard_failed_device(child);
        return 0;
    }
hub_child_enumerated:
    if (child->enum_failure != 0) {
        const char *failure = xhci_enum_failure_reason(child);

        if (!xhci_enum_failure_is_observable(failure)) {
            kprint("xhci: hub slot%u port%u enumerate failed reason=%s slot=%u status=%x change=%x speed=%u route=%x\n",
                   (uint32_t)hub->slot_id,
                   (uint32_t)port,
                   failure,
                   (uint32_t)child->slot_id,
                   (uint32_t)status,
                   (uint32_t)change,
                   (uint32_t)child->speed,
                   child->route_string);
            xhci_hub_port_latch_failure(hub, port, status);
            hub->hub_port_retry_after[port] = now + xhci_hub_enum_failure_retry_delay_ticks(failure);
            xhci_discard_failed_device(child);
            return 0;
        }

        kprint("xhci: hub slot%u port%u enumerated slot=%u note=%s status=%x speed=%u route=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               (uint32_t)child->slot_id,
               failure,
               (uint32_t)status,
               (uint32_t)child->speed,
               child->route_string);
    }
    xhci_rebind_fresh_msc_to_rootfs_if_needed(child, "hub-hotplug");
    xhci_hub_port_clear_failure(hub, port);
    hub->hub_port_retry_after[port] = 0u;
    return 1;
}

void xhci_probe_hub(struct xhci_enum_device *dev, const uint8_t *cfg, uint16_t cfg_len) {
    uint8_t hub_desc[32];
    uint8_t primary_desc;
    uint8_t fallback_desc;
    uint8_t port_count;
    uint16_t characteristics;
    uint8_t tt_think_time;
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
    characteristics = usb_read_u16le(hub_desc + 3);
    tt_think_time = 0u;
    if (hub_desc[1] == USB_DESC_HUB) {
        tt_think_time = (uint8_t)((characteristics >> 5) & 3u);
    }
    dev->hub_port_count = port_count;
    dev->hub_tt_think_time = tt_think_time;
    pwr_ms = (uint32_t)hub_desc[5] * 2u;
    if (pwr_ms < 100u) {
        pwr_ms = 100u;
    }
    kprint("xhci: hub descriptor slot%u rootport=%u ports=%u pwr=%u desc=%x len=%u ch=%x chars=%x tt=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev->port,
           (uint32_t)port_count,
           pwr_ms,
           (uint32_t)hub_desc[1],
           (uint32_t)hub_desc[0],
           (uint32_t)hub_desc[4],
           (uint32_t)characteristics,
           (uint32_t)dev->hub_tt_think_time);
    if (!xhci_update_hub_context(dev, port_count)) {
        kprint("xhci: hub slot%u context update failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (dev->speed >= XHCI_SPEED_SUPER) {
        if (!xhci_hub_set_depth(dev, dev->route_depth)) {
            kprint("xhci: hub slot%u set depth failed depth=%u\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)dev->route_depth);
            return;
        }
        kprint("xhci: hub slot%u depth set depth=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->route_depth);
    }
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)xhci_hub_set_port_feature(dev, port, USB_HUB_FEATURE_PORT_POWER);
    }
    xhci_delay_ms(pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        uint16_t status = 0u;
        uint16_t change = 0u;

        if (xhci_hub_get_port_status(dev, port, &status, &change)) {
            if (change != 0u || (status & USB_HUB_PORT_CONNECTION) != 0u) {
                xhci_log_hub_port_status("power-on", dev, port, status, change);
            }
        }
        (void)xhci_enumerate_hub_port(dev, port);
    }
}

static int xhci_reset_port(uint32_t port) {
    uint32_t offset = XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
    uint32_t portsc = xhci_read32(g_xhci.op, offset);
    uint32_t before = portsc;

    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) != 0u) {
        kprint("xhci: root port%u reset skip portsc=%x conn=%u en=%u\n",
               port,
               portsc,
               (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
               (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u));
        return (portsc & XHCI_PORTSC_PED) != 0u;
    }
    kprint("xhci: root port%u reset start portsc=%x speed=%u pwr=%u change=%x\n",
           port,
           portsc,
           (portsc >> 10) & 0x0fu,
           (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
           portsc & XHCI_PORTSC_CHANGE_BITS);
    xhci_write32(g_xhci.op, offset, (portsc & XHCI_PORTSC_RW_PRESERVE) | XHCI_PORTSC_PR);
    for (uint32_t i = 0; i < 1000000u; i++) {
        portsc = xhci_read32(g_xhci.op, offset);
        if ((portsc & XHCI_PORTSC_PR) == 0u) {
            break;
        }
    }
    kprint("xhci: root port%u reset done before=%x after=%x conn=%u en=%u speed=%u change=%x\n",
           port,
           before,
           portsc,
           (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
           (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
           (portsc >> 10) & 0x0fu,
           portsc & XHCI_PORTSC_CHANGE_BITS);
    return (portsc & XHCI_PORTSC_PED) != 0u;
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
    kprint("xhci: root port%u clear changes=%x portsc=%x conn=%u en=%u speed=%u\n",
           port,
           changes,
           portsc,
           (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
           (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
           (portsc >> 10) & 0x0fu);
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

static struct xhci_enum_device *xhci_find_device_by_slot(uint8_t controller_index, uint8_t slot_id) {
    if (slot_id == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == controller_index &&
            dev->slot_id == slot_id) {
            return dev;
        }
    }
    return 0;
}

static int xhci_same_rebind_attachment(struct xhci_enum_device *stale,
                                       struct xhci_enum_device *fresh) {
    if (stale == 0 || fresh == 0 ||
        stale == fresh ||
        stale->controller_index != fresh->controller_index ||
        stale->port != fresh->port ||
        stale->route_string != fresh->route_string) {
        return 0;
    }
    if (stale->parent_slot_id == fresh->parent_slot_id &&
        stale->parent_port == fresh->parent_port) {
        return 1;
    }
    return stale->parent_slot_id == 0u &&
           fresh->parent_slot_id != 0u &&
           stale->parent_port == 0u;
}

static struct xhci_enum_device *xhci_find_rootfs_rebind_target(struct xhci_enum_device *fresh) {
    if (fresh == 0 || fresh->blockdev.driver_data != fresh) {
        return 0;
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *stale = &g_enum_devices[i];

        if (!stale->used ||
            stale == fresh ||
            stale->blockdev.driver_data != stale ||
            !blockdev_is_rootfs_protected(&stale->blockdev) ||
            !xhci_same_rebind_attachment(stale, fresh)) {
            continue;
        }
        return stale;
    }
    return 0;
}

static void xhci_rebind_fresh_msc_to_rootfs_if_needed(struct xhci_enum_device *fresh,
                                                      const char *reason) {
    struct xhci_enum_device *stale;
    uint8_t old_slot;
    uint8_t fresh_slot;

    if (fresh == 0 || fresh->blockdev.driver_data != fresh) {
        return;
    }
    stale = xhci_find_rootfs_rebind_target(fresh);
    if (stale == 0) {
        return;
    }
    old_slot = stale->slot_id;
    fresh_slot = fresh->slot_id;
    if (!xhci_msc_rebind_blockdev(stale, fresh)) {
        kprint("xhci: hotplug rebind failed old_slot=%u fresh_slot=%u reason=%s\n",
               (uint32_t)old_slot,
               (uint32_t)fresh_slot,
               reason != 0 ? reason : "?");
        return;
    }
    blockdev_record_rebind(&stale->blockdev, reason != 0 ? reason : "hotplug");
    if (stale->parent_slot_id == 0u) {
        g_xhci.root_port_slots[stale->port] = stale->slot_id;
    }
    kprint("xhci: hotplug rebind slot%u -> slot%u %s reason=%s\n",
           (uint32_t)old_slot,
           (uint32_t)stale->slot_id,
           stale->name,
           reason != 0 ? reason : "?");
}

int xhci_recover_parent_hub_port(struct xhci_enum_device *dev) {
    struct xhci_enum_device *hub;
    uint32_t portsc;
    uint16_t status = 0u;
    uint16_t change = 0u;

    if (dev == 0) {
        return 0;
    }
    if (dev->parent_slot_id == 0u) {
        if (dev->port == 0u || dev->port > g_xhci.max_ports) {
            return 0;
        }
        portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(dev->port));
        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            return 0;
        }
        if ((portsc & XHCI_PORTSC_PED) != 0u) {
            xhci_root_port_clear_failure(dev->port);
            return 1;
        }
        kprint("xhci: recover root port%u disabled portsc=%x\n",
               (uint32_t)dev->port,
               portsc);
        (void)xhci_reset_port(dev->port);
        xhci_delay_ms(20u);
        portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(dev->port));
        xhci_clear_root_port_changes(dev->port, portsc);
        if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
            kprint("xhci: recover root port%u enable failed portsc=%x\n",
                   (uint32_t)dev->port,
                   portsc);
            return 0;
        }
        xhci_root_port_clear_failure(dev->port);
        return 1;
    }
    if (dev->parent_port == 0u) {
        return 0;
    }
    hub = xhci_find_device_by_slot(dev->controller_index, dev->parent_slot_id);
    if (hub == 0) {
        return 0;
    }
    if (!xhci_hub_get_port_status(hub, dev->parent_port, &status, &change)) {
        return 0;
    }
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        return 0;
    }
    if ((status & USB_HUB_PORT_ENABLE) != 0u) {
        xhci_hub_port_clear_failure(hub, dev->parent_port);
        return 1;
    }
    kprint("xhci: recover hub slot%u port%u disabled status=%x change=%x\n",
           (uint32_t)hub->slot_id,
           (uint32_t)dev->parent_port,
           (uint32_t)status,
           (uint32_t)change);
    if (xhci_hub_port_uses_reset(hub)) {
        if (!xhci_hub_set_port_feature(hub, dev->parent_port, USB_HUB_FEATURE_PORT_RESET)) {
            return 0;
        }
        xhci_delay_ms(80u);
        for (uint32_t i = 0u; i < 200u; i++) {
            if (!xhci_hub_get_port_status(hub, dev->parent_port, &status, &change)) {
                return 0;
            }
            if ((status & USB_HUB_PORT_RESET) == 0u) {
                break;
            }
            xhci_delay_ms(1u);
        }
    } else {
        xhci_delay_ms(150u);
    }
    xhci_hub_clear_changes(hub, dev->parent_port, change);
    if (!xhci_hub_get_port_status(hub, dev->parent_port, &status, &change)) {
        return 0;
    }
    if ((status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("xhci: recover hub slot%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)dev->parent_port,
               (uint32_t)status,
               (uint32_t)change);
        return 0;
    }
    xhci_hub_clear_changes(hub, dev->parent_port, change);
    xhci_hub_port_clear_failure(hub, dev->parent_port);
    return 1;
}

int xhci_reenumerate_and_rebind_blockdev(struct xhci_enum_device *dev) {
    struct xhci_enum_device *fresh;
    struct xhci_enum_device *hub = 0;
    uint32_t portsc = 0u;
    uint32_t speed = 0u;
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint8_t old_slot;

    if (dev == 0 || dev->blockdev.driver_data != dev) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    old_slot = dev->slot_id;
    if (dev->parent_slot_id == 0u) {
        if (dev->port == 0u || dev->port > g_xhci.max_ports) {
            return 0;
        }
        (void)xhci_reset_port(dev->port);
        xhci_delay_ms(20u);
        portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(dev->port));
        xhci_clear_root_port_changes(dev->port, portsc);
        if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
            kprint("xhci: rebind root port%u enable failed portsc=%x\n",
                   (uint32_t)dev->port,
                   portsc);
            return 0;
        }
        speed = (portsc >> 10) & 0x0fu;
    } else {
        hub = xhci_find_device_by_slot(dev->controller_index, dev->parent_slot_id);
        if (hub == 0 || dev->parent_port == 0u) {
            return 0;
        }
        if (xhci_hub_port_uses_reset(hub)) {
            if (!xhci_hub_set_port_feature(hub, dev->parent_port, USB_HUB_FEATURE_PORT_RESET)) {
                return 0;
            }
            xhci_delay_ms(80u);
            for (uint32_t i = 0u; i < 200u; i++) {
                if (!xhci_hub_get_port_status(hub, dev->parent_port, &status, &change)) {
                    return 0;
                }
                if ((status & USB_HUB_PORT_RESET) == 0u) {
                    break;
                }
                xhci_delay_ms(1u);
            }
        } else {
            xhci_delay_ms(150u);
        }
        xhci_hub_clear_changes(hub, dev->parent_port, change);
        if (!xhci_hub_get_port_status(hub, dev->parent_port, &status, &change) ||
            (status & USB_HUB_PORT_CONNECTION) == 0u ||
            (status & USB_HUB_PORT_ENABLE) == 0u) {
            kprint("xhci: rebind hub slot%u port%u enable failed status=%x change=%x\n",
                   (uint32_t)(hub != 0 ? hub->slot_id : 0u),
                   (uint32_t)dev->parent_port,
                   (uint32_t)status,
                   (uint32_t)change);
            return 0;
        }
        speed = xhci_hub_child_speed(hub, status);
    }

    fresh = xhci_alloc_device_record();
    if (fresh == 0) {
        return 0;
    }
    memset(fresh, 0, sizeof(*fresh));
    fresh->port = dev->port;
    fresh->controller_index = dev->controller_index;
    fresh->speed = (uint8_t)speed;
    fresh->route_depth = dev->route_depth;
    fresh->route_string = dev->route_string;
    fresh->parent_slot_id = dev->parent_slot_id;
    fresh->parent_port = dev->parent_port;
    fresh->tt_hub_slot_id = dev->tt_hub_slot_id;
    fresh->tt_port = dev->tt_port;
    fresh->tt_think_time = dev->tt_think_time;

    if (!xhci_enumerate_device(fresh) ||
        fresh->enum_failure != 0 ||
        fresh->blockdev.driver_data != fresh ||
        !xhci_msc_rebind_blockdev(dev, fresh)) {
        const char *failure = xhci_enum_failure_reason(fresh);

        kprint("xhci: rebind enumerate failed old_slot=%u reason=%s\n",
               (uint32_t)old_slot,
               failure);
        xhci_discard_failed_device(fresh);
        return 0;
    }
    if (dev->parent_slot_id == 0u) {
        g_xhci.root_port_slots[dev->port] = dev->slot_id;
        xhci_root_port_clear_failure(dev->port);
    } else if (hub != 0) {
        xhci_hub_port_clear_failure(hub, dev->parent_port);
    }
    if (old_slot != 0u && old_slot != dev->slot_id) {
        struct xhci_enum_device stale;

        memset(&stale, 0, sizeof(stale));
        stale.controller_index = dev->controller_index;
        stale.slot_id = old_slot;
        xhci_disable_device_slot(&stale);
    }
    kprint("xhci: rebind slot%u -> slot%u %s\n",
           (uint32_t)old_slot,
           (uint32_t)dev->slot_id,
           dev->name);
    return 1;
}

static void xhci_mark_device_detached(struct xhci_enum_device *dev) {
    uint8_t slot_id;
    uint8_t msc_registered = 0u;
    uint8_t resources_retained = 0u;

    if (dev == 0 || !dev->used) {
        return;
    }
    slot_id = dev->slot_id;
    if (dev->blockdev.driver_data == dev) {
        if (blockdev_unregister(&dev->blockdev) != 0) {
            dev->msc_offline = blockdev_is_rootfs_protected(&dev->blockdev) ? 0u : 1u;
            dev->read_cache_valid = 0u;
            (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_OFFLINE);
            resources_retained = 1u;
        } else {
            msc_registered = 1u;
        }
    }
    if (dev->bulk_in_epid != 0u) {
        xhci_forget_deferred_transfer_events(slot_id, dev->bulk_in_epid);
    }
    if (dev->bulk_out_epid != 0u) {
        xhci_forget_deferred_transfer_events(slot_id, dev->bulk_out_epid);
    }
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
    for (uint32_t i = 0u; i < g_hid_mouse_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *mouse = &g_hid_mice[i];

        if (mouse->dev == dev) {
            mouse->present = 0u;
            mouse->poll_disabled = 1u;
            mouse->interrupt_pending = 0u;
            mouse->report_queue_head = 0u;
            mouse->report_queue_tail = 0u;
            mouse->report_queue_count = 0u;
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
    if (!resources_retained) {
        dev->used = 0u;
        dev->slot_id = 0u;
        dev->parent_slot_id = 0u;
        dev->parent_port = 0u;
        dev->tt_hub_slot_id = 0u;
        dev->tt_port = 0u;
        dev->tt_think_time = 0u;
        dev->hub_port_count = 0u;
        dev->hub_tt_think_time = 0u;
    }
    if (msc_registered && g_xhci_msc_count != 0u) {
        g_xhci_msc_count--;
    }
    if (!resources_retained &&
        (msc_registered || dev->blockdev.driver_data != dev)) {
        xhci_release_enum_device_resources(dev);
    }
}

static void xhci_discard_failed_device(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    xhci_disable_device_slot(dev);
    xhci_mark_device_detached(dev);
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
    uint32_t now;

    if (port == 0u || port > g_xhci.max_ports || port >= XHCI_MAX_ROOT_PORTS) {
        return 0;
    }
    now = xhci_hotplug_now_ticks();
    if (!xhci_retry_ready(now, g_xhci_root_port_retry_after[g_xhci_active_controller][port])) {
        return 0;
    }
    if (xhci_command_backoff_active()) {
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
            now + xhci_command_backoff_retry_delay_ticks();
        return 0;
    }
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    if ((portsc & (XHCI_PORTSC_CCS | XHCI_PORTSC_CHANGE_BITS)) != 0u) {
        kprint("xhci: %s root port%u scan portsc=%x tracked=%u conn=%u en=%u pwr=%u speed=%u change=%x\n",
               reason,
               port,
               portsc,
               (uint32_t)xhci_root_port_is_tracked((uint8_t)port),
               (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
               (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
               (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
               (portsc >> 10) & 0x0fu,
               portsc & XHCI_PORTSC_CHANGE_BITS);
    }
    if ((portsc & XHCI_PORTSC_CHANGE_BITS) != 0u) {
        xhci_root_port_clear_failure(port);
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] = 0u;
    }
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u) {
        xhci_root_port_clear_failure(port);
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] = 0u;
        return 0;
    }
    if (xhci_root_port_is_tracked((uint8_t)port) ||
        xhci_root_port_failure_latched(port, portsc)) {
        return 0;
    }
    xhci_delay_ms(20u);
    (void)xhci_reset_port(port);
    xhci_delay_ms(20u);
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
        kprint("xhci: %s root port%u enable failed portsc=%x\n", reason, port, portsc);
        xhci_root_port_latch_failure(port, portsc);
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
            now + xhci_root_retry_delay_ticks();
        return 0;
    }
    speed = (portsc >> 10) & 0x0fu;
    kprint("xhci: %s root port%u enabled speed=%u powered=%u portsc=%x\n",
           reason,
           port,
           speed,
           (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
           portsc);
    dev = xhci_alloc_device_record();
    if (dev == 0) {
        kprint("xhci: no enum slots left\n");
        xhci_root_port_latch_failure(port, portsc);
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
            now + xhci_root_retry_delay_ticks();
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->port = (uint8_t)port;
    dev->controller_index = g_xhci_active_controller;
    dev->speed = (uint8_t)speed;
    if (!xhci_enumerate_device(dev)) {
        const char *failure = xhci_enum_failure_reason(dev);

        kprint("xhci: %s root port%u enumerate failed reason=%s portsc=%x speed=%u\n",
               reason,
               port,
               failure,
               portsc,
               speed);
        g_xhci.root_port_slots[port] = 0u;
        xhci_root_port_latch_failure(port, portsc);
        g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
            now + xhci_enum_failure_retry_delay_ticks(failure);
        xhci_discard_failed_device(dev);
        return 0;
    }
    g_xhci.root_port_slots[port] = dev->slot_id;
    if (dev->enum_failure != 0) {
        const char *failure = xhci_enum_failure_reason(dev);

        if (!xhci_enum_failure_is_observable(failure)) {
            kprint("xhci: %s root port%u enumerate failed reason=%s slot=%u portsc=%x speed=%u\n",
                   reason,
                   port,
                   failure,
                   (uint32_t)dev->slot_id,
                   portsc,
                   speed);
            g_xhci.root_port_slots[port] = 0u;
            xhci_root_port_latch_failure(port, portsc);
            g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
                now + xhci_enum_failure_retry_delay_ticks(failure);
            xhci_discard_failed_device(dev);
            return 0;
        }

        kprint("xhci: %s root port%u enumerated slot=%u note=%s portsc=%x speed=%u\n",
               reason,
               port,
               (uint32_t)dev->slot_id,
               failure,
               portsc,
               speed);
    }
    xhci_rebind_fresh_msc_to_rootfs_if_needed(dev, reason);
    xhci_root_port_clear_failure(port);
    g_xhci_root_port_retry_after[g_xhci_active_controller][port] = 0u;
    return 1;
}

void xhci_enumerate_connected_ports(void) {
    kprint("xhci: root scan begin ports=%u controller=%u\n",
           (uint32_t)g_xhci.max_ports,
           (uint32_t)g_xhci_active_controller);
    for (uint32_t port = 1; port <= g_xhci.max_ports; port++) {
        (void)xhci_enumerate_root_port(port, "boot");
    }
    kprint("xhci: root scan end controller=%u\n", (uint32_t)g_xhci_active_controller);
}

static void xhci_scan_root_hotplug_ports(void) {
    if (xhci_command_backoff_active()) {
        return;
    }
    for (uint32_t port = 1u; port <= g_xhci.max_ports && port < XHCI_MAX_ROOT_PORTS; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
        struct xhci_enum_device *dev = xhci_find_root_port_device(g_xhci_active_controller, (uint8_t)port);
        uint32_t changes = portsc & XHCI_PORTSC_CHANGE_BITS;

        if (changes != 0u) {
            kprint("xhci: hotplug root port%u status change portsc=%x tracked=%u conn=%u en=%u speed=%u change=%x\n",
                   port,
                   portsc,
                   (uint32_t)(dev != 0 || g_xhci.root_port_slots[port] != 0u),
                   (uint32_t)((portsc & XHCI_PORTSC_CCS) != 0u),
                   (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
                   (portsc >> 10) & 0x0fu,
                   changes);
            xhci_root_port_clear_failure(port);
            g_xhci_root_port_retry_after[g_xhci_active_controller][port] = 0u;
        }
        xhci_clear_root_port_changes(port, portsc);
        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
                if (g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] == 0u) {
                    kprint("xhci: hotplug root port%u disconnect candidate portsc=%x\n",
                           port,
                           portsc);
                    g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 1u;
                    g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
                        xhci_hotplug_now_ticks() + xhci_disconnect_confirm_delay_ticks();
                    continue;
                }
                kprint("xhci: hotplug root port%u disconnected portsc=%x\n", port, portsc);
                g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 0u;
                xhci_mark_device_detached(dev);
                g_xhci.root_port_slots[port] = 0u;
            }
            g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 0u;
            xhci_root_port_clear_failure(port);
            g_xhci_root_port_retry_after[g_xhci_active_controller][port] = 0u;
            continue;
        }
        if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
            if ((portsc & XHCI_PORTSC_PED) == 0u) {
                if (g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] == 0u) {
                    kprint("xhci: hotplug root port%u disabled candidate portsc=%x\n",
                           port,
                           portsc);
                    g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 1u;
                    g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
                        xhci_hotplug_now_ticks() + xhci_disconnect_confirm_delay_ticks();
                    continue;
                }
                kprint("xhci: hotplug root port%u reenumerate disabled portsc=%x\n",
                       port,
                       portsc);
                g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 0u;
                if (dev != 0 && dev->blockdev.driver_data == dev) {
                    dev->msc_offline = 0u;
                    (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_ONLINE);
                    xhci_root_port_latch_failure(port, portsc);
                    g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
                        xhci_hotplug_now_ticks() + xhci_root_retry_delay_ticks();
                    continue;
                }
                xhci_mark_device_detached(dev);
                g_xhci.root_port_slots[port] = 0u;
                xhci_root_port_latch_failure(port, portsc);
                g_xhci_root_port_retry_after[g_xhci_active_controller][port] =
                    xhci_hotplug_now_ticks() + xhci_root_retry_delay_ticks();
                continue;
            }
            g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 0u;
            continue;
        }
        g_xhci_root_port_disconnect_pending[g_xhci_active_controller][port] = 0u;
        if (xhci_root_port_failure_latched(port, portsc)) {
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
        uint32_t now = xhci_hotplug_now_ticks();

        child = xhci_find_hub_child(hub, port);
        if (!xhci_retry_ready(now, hub->hub_port_retry_after[port])) {
            continue;
        }
        if (xhci_command_backoff_active()) {
            hub->hub_port_retry_after[port] = now + xhci_command_backoff_retry_delay_ticks();
            continue;
        }
        if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
            kprint("xhci: hotplug hub slot%u port%u status failed\n",
                   (uint32_t)hub->slot_id,
                   (uint32_t)port);
            hub->hub_port_retry_after[port] = now + xhci_hub_retry_delay_ticks();
            continue;
        }
        if (change != 0u) {
            xhci_log_hub_port_status("hotplug", hub, port, status, change);
            xhci_hub_port_clear_failure(hub, port);
            hub->hub_port_retry_after[port] = 0u;
        }
        xhci_hub_clear_changes(hub, port, change);
        if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
            if (child != 0) {
                if (hub->hub_port_disconnect_pending[port] == 0u) {
                    kprint("xhci: hotplug hub slot%u port%u disconnect candidate status=%x change=%x\n",
                           (uint32_t)hub->slot_id,
                           (uint32_t)port,
                           (uint32_t)status,
                           (uint32_t)change);
                    hub->hub_port_disconnect_pending[port] = 1u;
                    hub->hub_port_retry_after[port] =
                        now + xhci_disconnect_confirm_delay_ticks();
                    continue;
                }
                kprint("xhci: hotplug hub slot%u port%u disconnected status=%x change=%x\n",
                       (uint32_t)hub->slot_id,
                       (uint32_t)port,
                       (uint32_t)status,
                       (uint32_t)change);
                hub->hub_port_disconnect_pending[port] = 0u;
                xhci_mark_device_detached(child);
            }
            hub->hub_port_disconnect_pending[port] = 0u;
            xhci_hub_port_clear_failure(hub, port);
            hub->hub_port_retry_after[port] = 0u;
            continue;
        }
        if (child != 0) {
            if ((status & USB_HUB_PORT_ENABLE) == 0u) {
                if (hub->hub_port_disconnect_pending[port] == 0u) {
                    kprint("xhci: hotplug hub slot%u port%u disabled candidate status=%x change=%x\n",
                           (uint32_t)hub->slot_id,
                           (uint32_t)port,
                           (uint32_t)status,
                           (uint32_t)change);
                    hub->hub_port_disconnect_pending[port] = 1u;
                    hub->hub_port_retry_after[port] =
                        now + xhci_disconnect_confirm_delay_ticks();
                    continue;
                }
                kprint("xhci: hotplug hub slot%u port%u reenumerate disabled status=%x change=%x\n",
                       (uint32_t)hub->slot_id,
                       (uint32_t)port,
                       (uint32_t)status,
                       (uint32_t)change);
                hub->hub_port_disconnect_pending[port] = 0u;
                if (child->blockdev.driver_data == child) {
                    child->msc_offline = 0u;
                    (void)blockdev_set_state(&child->blockdev, BLOCKDEV_STATE_ONLINE);
                    xhci_hub_port_latch_failure(hub, port, status);
                    hub->hub_port_retry_after[port] =
                        now + xhci_hub_retry_delay_ticks();
                    continue;
                }
                xhci_mark_device_detached(child);
                xhci_hub_port_latch_failure(hub, port, status);
                hub->hub_port_retry_after[port] =
                    now + xhci_hub_retry_delay_ticks();
                continue;
            }
            hub->hub_port_disconnect_pending[port] = 0u;
            continue;
        }
        hub->hub_port_disconnect_pending[port] = 0u;
        if (xhci_hub_port_failure_latched(hub, port, status)) {
            continue;
        }
        if (hub->speed >= XHCI_SPEED_SUPER) {
            kprint("xhci: hotplug hub slot%u port%u deferred ss child status=%x change=%x\n",
                   (uint32_t)hub->slot_id,
                   (uint32_t)port,
                   (uint32_t)status,
                   (uint32_t)change);
            xhci_hub_port_latch_failure(hub, port, status);
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
    if (xhci_command_backoff_active()) {
        return;
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == g_xhci_active_controller &&
            dev->hub_port_count != 0u) {
            xhci_scan_hub_hotplug(dev);
        }
    }
}

void xhci_hotplug_poll(void) {
    uint32_t tick;

    if (g_xhci_controller_count == 0u) {
        return;
    }
    if (!xhci_try_begin_busy()) {
        return;
    }
    tick = hal_timer_current_ticks();
    if ((uint32_t)(tick - g_xhci_last_hotplug_tick) < XHCI_HOTPLUG_SCAN_TICKS) {
        xhci_end_busy();
        return;
    }
    g_xhci_last_hotplug_tick = tick;
    for (uint32_t index = 0u; index < g_xhci_controller_count && index < XHCI_MAX_CONTROLLERS; index++) {
        if (!xhci_select_controller((uint8_t)index)) {
            continue;
        }
        xhci_service_deferred_slot_disables();
        xhci_scan_root_hotplug_ports();
        xhci_scan_hub_hotplug_ports();
        xhci_save_active_controller();
    }
    xhci_end_busy();
}

void xhci_hotplug_scan_now(void) {
    if (g_xhci_controller_count == 0u) {
        return;
    }
    if (!xhci_try_begin_busy()) {
        return;
    }
    for (uint32_t index = 0u; index < g_xhci_controller_count && index < XHCI_MAX_CONTROLLERS; index++) {
        if (!xhci_select_controller((uint8_t)index)) {
            continue;
        }
        xhci_service_deferred_slot_disables();
        xhci_scan_root_hotplug_ports();
        xhci_scan_hub_hotplug_ports();
        xhci_save_active_controller();
    }
    xhci_end_busy();
}
