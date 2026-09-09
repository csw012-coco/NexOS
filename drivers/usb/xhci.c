#include "drivers/usb/xhci_internal.h"

enum {
    XHCI_DEFERRED_TRANSFER_EVENTS = 32u,
    XHCI_DEFERRED_COMMAND_EVENTS = 16u,
    XHCI_DEFERRED_SLOT_DISABLES = 16u,
    XHCI_STALE_TRANSFER_LOG_LIMIT = 8u,
    XHCI_COMMAND_TIMEOUT_LOG_LIMIT = 16u,
    XHCI_DEFERRED_SLOT_DISABLE_RETRY_MS = 5000u
};

struct xhci_deferred_transfer_event {
    uint8_t used;
    uint8_t controller_index;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint32_t completion;
    uint32_t residual;
    uint64_t trb_phys;
};

struct xhci_deferred_command_event {
    uint8_t used;
    uint8_t controller_index;
    uint8_t slot_id;
    uint32_t completion;
    uint64_t trb_phys;
};

struct xhci_deferred_slot_disable {
    uint8_t used;
    uint8_t controller_index;
    uint8_t slot_id;
    uint8_t retry_logged;
    uint32_t retry_after;
};

enum xhci_wait_kind {
    XHCI_WAIT_COMMAND,
    XHCI_WAIT_TRANSFER
};

struct xhci_wait_request {
    enum xhci_wait_kind kind;
    struct xhci_enum_device *dev;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint64_t expected_trb_phys;
    uint32_t expected_index;
    uint32_t expected_generation;
    uint8_t matched_slot_id;
    uint32_t completion;
    uint32_t residual;
};

static struct xhci_deferred_transfer_event g_xhci_deferred_transfer_events[XHCI_DEFERRED_TRANSFER_EVENTS];
static struct xhci_deferred_command_event g_xhci_deferred_command_events[XHCI_DEFERRED_COMMAND_EVENTS];
static struct xhci_deferred_slot_disable g_xhci_deferred_slot_disables[XHCI_DEFERRED_SLOT_DISABLES];
static uint8_t g_xhci_deferred_drop_logged;
static uint8_t g_xhci_deferred_command_drop_logged;
static uint8_t g_xhci_deferred_slot_disable_drop_logged;
static uint32_t g_xhci_stale_transfer_events;
static uint32_t g_xhci_command_timeouts;
static uint32_t g_xhci_command_retry_after;

static void xhci_dma_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static uint32_t xhci_ms_to_ticks(uint32_t ms) {
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

static int xhci_command_ready(void) {
    uint32_t now = hal_timer_current_ticks();

    return g_xhci_command_retry_after == 0u ||
           (int32_t)(now - g_xhci_command_retry_after) >= 0;
}

static void xhci_command_backoff(uint32_t ms) {
    g_xhci_command_retry_after = hal_timer_current_ticks() + xhci_ms_to_ticks(ms);
}

int xhci_command_backoff_active(void) {
    return !xhci_command_ready();
}

static void xhci_set_enum_failure(struct xhci_enum_device *dev, const char *reason) {
    if (dev != 0 && dev->enum_failure == 0) {
        dev->enum_failure = reason;
    }
}

const char *xhci_enum_failure_reason(const struct xhci_enum_device *dev) {
    if (dev == 0 || dev->enum_failure == 0) {
        return "unknown";
    }
    return dev->enum_failure;
}

static void xhci_release_failed_enum_device(struct xhci_enum_device *dev) {
    const char *failure;
    uint8_t speed;
    uint8_t port;
    uint8_t parent_slot_id;
    uint8_t parent_port;
    uint8_t tt_hub_slot_id;
    uint8_t tt_port;
    uint8_t tt_think_time;
    uint8_t route_depth;
    uint32_t route_string;
    uint32_t controller_index;

    if (dev == 0) {
        return;
    }
    failure = dev->enum_failure;
    speed = dev->speed;
    port = dev->port;
    parent_slot_id = dev->parent_slot_id;
    parent_port = dev->parent_port;
    tt_hub_slot_id = dev->tt_hub_slot_id;
    tt_port = dev->tt_port;
    tt_think_time = dev->tt_think_time;
    route_depth = dev->route_depth;
    route_string = dev->route_string;
    controller_index = dev->controller_index;

    xhci_release_enum_device_resources(dev);

    dev->enum_failure = failure;
    dev->speed = speed;
    dev->port = port;
    dev->parent_slot_id = parent_slot_id;
    dev->parent_port = parent_port;
    dev->tt_hub_slot_id = tt_hub_slot_id;
    dev->tt_port = tt_port;
    dev->tt_think_time = tt_think_time;
    dev->route_depth = route_depth;
    dev->route_string = route_string;
    dev->controller_index = controller_index;
}

static void xhci_ack_pending_status(void) {
    if (g_xhci.op == 0) {
        return;
    }
    xhci_write32(g_xhci.op, XHCI_OP_USBSTS, XHCI_USBSTS_EINT | XHCI_USBSTS_PCD);
}

static void xhci_log_stale_transfer_event(const char *source,
                                          uint8_t slot_id,
                                          uint8_t endpoint_id,
                                          uint64_t got_trb_phys,
                                          uint64_t expected_trb_phys,
                                          uint32_t completion) {
    g_xhci_stale_transfer_events++;
    if (XHCI_EVENT_TRACE != 0u && g_xhci_stale_transfer_events <= XHCI_STALE_TRANSFER_LOG_LIMIT) {
        kprint("xhci: stale %s event slot=%u ep=%u got=%lx want=%lx cc=%u\n",
               source,
               (uint32_t)slot_id,
               (uint32_t)endpoint_id,
               got_trb_phys,
               expected_trb_phys,
               completion);
    }
}

static int xhci_take_deferred_command_event(uint64_t expected_trb_phys,
                                            uint8_t *slot_id_out,
                                            uint32_t *completion_out) {
    for (uint32_t i = 0u; i < XHCI_DEFERRED_COMMAND_EVENTS; i++) {
        struct xhci_deferred_command_event *event = &g_xhci_deferred_command_events[i];

        if (!event->used ||
            event->controller_index != g_xhci_active_controller) {
            continue;
        }
        if (expected_trb_phys != 0u &&
            (event->trb_phys & ~0xfull) != (expected_trb_phys & ~0xfull)) {
            continue;
        }
        if (slot_id_out != 0) {
            *slot_id_out = event->slot_id;
        }
        if (completion_out != 0) {
            *completion_out = event->completion;
        }
        event->used = 0u;
        return 1;
    }
    return 0;
}

static void xhci_defer_command_event(uint8_t slot_id,
                                     uint32_t completion,
                                     uint64_t trb_phys) {
    for (uint32_t i = 0u; i < XHCI_DEFERRED_COMMAND_EVENTS; i++) {
        struct xhci_deferred_command_event *event = &g_xhci_deferred_command_events[i];

        if (event->used) {
            continue;
        }
        event->used = 1u;
        event->controller_index = g_xhci_active_controller;
        event->slot_id = slot_id;
        event->completion = completion;
        event->trb_phys = trb_phys;
        return;
    }
    if (!g_xhci_deferred_command_drop_logged) {
        g_xhci_deferred_command_drop_logged = 1u;
        kprint("xhci: deferred command event queue full\n");
    }
}

static int xhci_take_deferred_transfer_event(uint8_t slot_id,
                                             uint8_t endpoint_id,
                                             struct xhci_enum_device *dev,
                                             uint64_t expected_trb_phys,
                                             uint32_t expected_index,
                                             uint32_t expected_generation,
                                             uint32_t *completion_out,
                                             uint32_t *residual_out) {
    for (uint32_t i = 0u; i < XHCI_DEFERRED_TRANSFER_EVENTS; i++) {
        struct xhci_deferred_transfer_event *event = &g_xhci_deferred_transfer_events[i];

        if (!event->used ||
            event->controller_index != g_xhci_active_controller ||
            event->slot_id != slot_id ||
            event->endpoint_id != endpoint_id) {
            continue;
        }
        if (expected_trb_phys != 0u &&
            (event->trb_phys & ~0xfull) != (expected_trb_phys & ~0xfull)) {
            xhci_log_stale_transfer_event("deferred",
                                          slot_id,
                                          endpoint_id,
                                          event->trb_phys,
                                          expected_trb_phys,
                                          event->completion);
            event->used = 0u;
            continue;
        }
        if (dev != 0 && expected_generation != 0u) {
            const uint32_t *generations = 0;
            const uint8_t *active = 0;

            if (expected_index >= XHCI_RING_TRBS) {
                event->used = 0u;
                continue;
            }
            if (endpoint_id == dev->bulk_in_epid) {
                generations = dev->bulk_in_meta_generation;
                active = dev->bulk_in_meta_active;
            } else if (endpoint_id == dev->bulk_out_epid) {
                generations = dev->bulk_out_meta_generation;
                active = dev->bulk_out_meta_active;
            }
            if (generations == 0 || active == 0 ||
                active[expected_index] == 0u ||
                generations[expected_index] != expected_generation) {
                xhci_log_stale_transfer_event("deferred-generation",
                                              slot_id,
                                              endpoint_id,
                                              event->trb_phys,
                                              expected_trb_phys,
                                              event->completion);
                event->used = 0u;
                continue;
            }
        }
        if (completion_out != 0) {
            *completion_out = event->completion;
        }
        if (residual_out != 0) {
            *residual_out = event->residual;
        }
        event->used = 0u;
        return 1;
    }
    return 0;
}

void xhci_forget_deferred_transfer_events(uint8_t slot_id, uint8_t endpoint_id) {
    for (uint32_t i = 0u; i < XHCI_DEFERRED_TRANSFER_EVENTS; i++) {
        struct xhci_deferred_transfer_event *event = &g_xhci_deferred_transfer_events[i];

        if (!event->used ||
            event->controller_index != g_xhci_active_controller ||
            event->slot_id != slot_id ||
            event->endpoint_id != endpoint_id) {
            continue;
        }
        event->used = 0u;
    }
}

static void xhci_defer_transfer_event(uint8_t slot_id,
                                      uint8_t endpoint_id,
                                      uint32_t completion,
                                      uint32_t residual,
                                      uint64_t trb_phys) {
    if (xhci_hid_defer_transfer_event(slot_id, endpoint_id, completion, trb_phys)) {
        return;
    }
    for (uint32_t i = 0u; i < XHCI_DEFERRED_TRANSFER_EVENTS; i++) {
        struct xhci_deferred_transfer_event *event = &g_xhci_deferred_transfer_events[i];

        if (event->used) {
            continue;
        }
        event->used = 1u;
        event->controller_index = g_xhci_active_controller;
        event->slot_id = slot_id;
        event->endpoint_id = endpoint_id;
        event->completion = completion;
        event->residual = residual;
        event->trb_phys = trb_phys;
        return;
    }
    if (!g_xhci_deferred_drop_logged) {
        g_xhci_deferred_drop_logged = 1u;
        kprint("xhci: deferred transfer event queue full\n");
    }
}

static uint64_t xhci_ring_command(uint64_t parameter, uint32_t status, uint32_t control) {
    struct xhci_trb *trb = &g_xhci.command_ring[g_xhci.command_enqueue];
    uint64_t trb_phys = g_xhci.command_ring_phys +
                        (uint64_t)g_xhci.command_enqueue * XHCI_TRB_SIZE;

    xhci_ring_trb_set(trb, parameter, status, control | g_xhci.command_cycle);
    g_xhci.command_enqueue++;
    if (g_xhci.command_enqueue >= XHCI_RING_TRBS - 1u) {
        g_xhci.command_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (g_xhci.command_cycle != 0u ? 1u : 0u);
        g_xhci.command_cycle ^= 1u;
        g_xhci.command_enqueue = 0u;
    }
    xhci_dma_barrier();
    xhci_write32(g_xhci.doorbell, 0u, 0u);
    return trb_phys;
}

static int xhci_pop_event(struct xhci_trb *out) {
    struct xhci_trb *event = &g_xhci.event_ring[g_xhci.event_dequeue];
    uint32_t control = event->control;

    if (out == 0 || (control & 1u) != g_xhci.event_cycle) {
        return 0;
    }
    if (XHCI_EVENT_TRACE != 0u) {
        kprint("xhci: pop ev idx=%u cyc=%u type=%u slot=%u ep=%u cc=%u p=%x:%x st=%x ctl=%x\n",
               g_xhci.event_dequeue,
               g_xhci.event_cycle,
               xhci_trb_type(control),
               (control >> 24) & 0xffu,
               (control >> 16) & 0x1fu,
               (event->status >> 24) & 0xffu,
               event->parameter_hi,
               event->parameter_lo,
               event->status,
               event->control);
    }
    *out = *event;
    g_xhci.event_dequeue++;
    if (g_xhci.event_dequeue >= XHCI_RING_TRBS) {
        g_xhci.event_dequeue = 0u;
        g_xhci.event_cycle ^= 1u;
    }
    {
        uint64_t erdp = g_xhci.event_ring_phys +
                        (uint64_t)g_xhci.event_dequeue * XHCI_TRB_SIZE;

        __asm__ __volatile__("" ::: "memory");
        xhci_write64(g_xhci.runtime + XHCI_RUNTIME_INTR0, XHCI_INTR_ERDP,
                     erdp | XHCI_ERDP_EHB);
        xhci_ack_pending_status();
    }
    return 1;
}

static int xhci_command_event_matches(uint64_t got_trb_phys,
                                      uint64_t expected_trb_phys) {
    return expected_trb_phys == 0u ||
           (got_trb_phys & ~0xfull) == (expected_trb_phys & ~0xfull);
}

static int xhci_transfer_event_matches(const struct xhci_wait_request *wait,
                                       uint8_t slot_id,
                                       uint8_t endpoint_id,
                                       uint64_t trb_phys,
                                       uint32_t completion) {
    if (wait == 0 ||
        wait->kind != XHCI_WAIT_TRANSFER ||
        wait->slot_id != slot_id ||
        wait->endpoint_id != endpoint_id) {
        return 0;
    }
    if (wait->expected_trb_phys != 0u &&
        (trb_phys & ~0xfull) != (wait->expected_trb_phys & ~0xfull)) {
        xhci_log_stale_transfer_event("transfer",
                                      slot_id,
                                      endpoint_id,
                                      trb_phys,
                                      wait->expected_trb_phys,
                                      completion);
        return 0;
    }
    if (wait->dev != 0 && wait->expected_generation != 0u) {
        const uint32_t *generations = 0;
        const uint8_t *active = 0;

        if (wait->expected_index >= XHCI_RING_TRBS) {
            return 0;
        }
        if (endpoint_id == wait->dev->bulk_in_epid) {
            generations = wait->dev->bulk_in_meta_generation;
            active = wait->dev->bulk_in_meta_active;
        } else if (endpoint_id == wait->dev->bulk_out_epid) {
            generations = wait->dev->bulk_out_meta_generation;
            active = wait->dev->bulk_out_meta_active;
        }
        if (generations == 0 || active == 0 ||
            active[wait->expected_index] == 0u ||
            generations[wait->expected_index] != wait->expected_generation) {
            xhci_log_stale_transfer_event("transfer-generation",
                                          slot_id,
                                          endpoint_id,
                                          trb_phys,
                                          wait->expected_trb_phys,
                                          completion);
            return 0;
        }
    }
    return 1;
}

static int xhci_pump_event_for_wait(struct xhci_wait_request *wait) {
    struct xhci_trb event;
    uint32_t control;
    uint32_t type;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint32_t completion;
    uint64_t trb_phys;

    if (wait == 0 || !xhci_pop_event(&event)) {
        return 0;
    }

    control = event.control;
    type = xhci_trb_type(control);
    slot_id = (uint8_t)((control >> 24) & 0xffu);
    completion = (event.status >> 24) & 0xffu;
    trb_phys = ((uint64_t)event.parameter_hi << 32) | event.parameter_lo;

    if (type == XHCI_TRB_COMMAND_COMPLETION) {
        if (wait->kind == XHCI_WAIT_COMMAND &&
            xhci_command_event_matches(trb_phys, wait->expected_trb_phys)) {
            wait->matched_slot_id = slot_id;
            wait->completion = completion;
            return 1;
        }
        xhci_defer_command_event(slot_id, completion, trb_phys);
        return 0;
    }

    if (type == XHCI_TRB_TRANSFER_EVENT) {
        endpoint_id = (uint8_t)((control >> 16) & 0x1fu);
        if (xhci_transfer_event_matches(wait,
                                        slot_id,
                                        endpoint_id,
                                        trb_phys,
                                        completion)) {
            wait->completion = completion;
            wait->residual = event.status & 0x00ffffffu;
            return 1;
        }
        if (wait->kind != XHCI_WAIT_TRANSFER ||
            wait->slot_id != slot_id ||
            wait->endpoint_id != endpoint_id) {
            xhci_defer_transfer_event(slot_id,
                                      endpoint_id,
                                      completion,
                                      event.status & 0x00ffffffu,
                                      trb_phys);
        }
    }

    return 0;
}

static int xhci_wait_command_completion(uint64_t expected_trb_phys,
                                        uint8_t *slot_id_out,
                                        uint32_t *completion_out) {
    struct xhci_wait_request wait;
    uint32_t usbsts;
    uint64_t crcr;

    if (xhci_take_deferred_command_event(expected_trb_phys, slot_id_out, completion_out)) {
        return 1;
    }
    wait.kind = XHCI_WAIT_COMMAND;
    wait.dev = 0;
    wait.slot_id = 0u;
    wait.endpoint_id = 0u;
    wait.expected_trb_phys = expected_trb_phys;
    wait.expected_index = 0u;
    wait.expected_generation = 0u;
    wait.matched_slot_id = 0u;
    wait.completion = 0u;
    for (uint32_t spin = 0; spin < XHCI_WAIT_SPINS_DEFAULT; spin++) {
        if (xhci_pump_event_for_wait(&wait)) {
            if (slot_id_out != 0) {
                *slot_id_out = wait.matched_slot_id;
            }
            if (completion_out != 0) {
                *completion_out = wait.completion;
            }
            return 1;
        }
        hal_cpu_relax();
    }
    for (uint32_t i = 0u; i < 16u; i++) {
        if (xhci_pump_event_for_wait(&wait)) {
            if (slot_id_out != 0) {
                *slot_id_out = wait.matched_slot_id;
            }
            if (completion_out != 0) {
                *completion_out = wait.completion;
            }
            return 1;
        }
    }
    xhci_ack_pending_status();
    usbsts = xhci_read32(g_xhci.op, XHCI_OP_USBSTS);
    crcr = (uint64_t)xhci_read32(g_xhci.op, XHCI_OP_CRCR) |
           ((uint64_t)xhci_read32(g_xhci.op, XHCI_OP_CRCR + 4u) << 32);
    g_xhci_command_timeouts++;
    if (g_xhci_command_timeouts <= XHCI_COMMAND_TIMEOUT_LOG_LIMIT) {
        kprint("xhci: command timeout want=%lx enqueue=%u cycle=%u evdq=%u evcycle=%u usbsts=%x crcr=%lx\n",
               expected_trb_phys,
               g_xhci.command_enqueue,
               (uint32_t)g_xhci.command_cycle,
               g_xhci.event_dequeue,
               (uint32_t)g_xhci.event_cycle,
               usbsts,
               crcr);
    }
    xhci_command_backoff(5000u);
    return 0;
}

int xhci_wait_transfer_event_generation_spins(struct xhci_enum_device *dev,
                                              uint8_t slot_id,
                                              uint8_t endpoint_id,
                                              uint32_t *completion_out,
                                              uint32_t *residual_out,
                                              uint64_t expected_trb_phys,
                                              uint32_t expected_index,
                                              uint32_t expected_generation,
                                              uint32_t max_spins);

int xhci_wait_transfer_event_spins(uint8_t slot_id,
                                   uint8_t endpoint_id,
                                   uint32_t *completion_out,
                                   uint64_t expected_trb_phys,
                                   uint32_t max_spins) {
    return xhci_wait_transfer_event_generation_spins(0,
                                                     slot_id,
                                                     endpoint_id,
                                                     completion_out,
                                                     0,
                                                     expected_trb_phys,
                                                     0u,
                                                     0u,
                                                     max_spins);
}

int xhci_wait_transfer_event_generation_spins(struct xhci_enum_device *dev,
                                              uint8_t slot_id,
                                              uint8_t endpoint_id,
                                              uint32_t *completion_out,
                                              uint32_t *residual_out,
                                              uint64_t expected_trb_phys,
                                              uint32_t expected_index,
                                              uint32_t expected_generation,
                                              uint32_t max_spins) {
    struct xhci_wait_request wait;

    if (xhci_take_deferred_transfer_event(slot_id,
                                          endpoint_id,
                                          dev,
                                          expected_trb_phys,
                                          expected_index,
                                          expected_generation,
                                          completion_out,
                                          residual_out)) {
        return 1;
    }
    wait.kind = XHCI_WAIT_TRANSFER;
    wait.dev = dev;
    wait.slot_id = slot_id;
    wait.endpoint_id = endpoint_id;
    wait.expected_trb_phys = expected_trb_phys;
    wait.expected_index = expected_index;
    wait.expected_generation = expected_generation;
    wait.matched_slot_id = 0u;
    wait.completion = 0u;
    wait.residual = 0u;
    for (uint32_t spin = 0; spin < max_spins; spin++) {
        if (dev != 0 && blockdev_request_cancelled(&dev->blockdev)) {
            return 0;
        }
        if (xhci_pump_event_for_wait(&wait)) {
            if (completion_out != 0) {
                *completion_out = wait.completion;
            }
            if (residual_out != 0) {
                *residual_out = wait.residual;
            }
            return 1;
        }
        hal_cpu_relax();
    }
    return 0;
}

int xhci_wait_transfer_event_generation_ms(struct xhci_enum_device *dev,
                                           uint8_t slot_id,
                                           uint8_t endpoint_id,
                                           uint32_t *completion_out,
                                           uint32_t *residual_out,
                                           uint64_t expected_trb_phys,
                                           uint32_t expected_index,
                                           uint32_t expected_generation,
                                           uint32_t timeout_ms) {
    struct xhci_wait_request wait;
    uint32_t wait_ticks;
    uint32_t start;
    uint32_t last;
    uint32_t stagnant_spins = 0u;

    if (xhci_take_deferred_transfer_event(slot_id,
                                          endpoint_id,
                                          dev,
                                          expected_trb_phys,
                                          expected_index,
                                          expected_generation,
                                          completion_out,
                                          residual_out)) {
        return 1;
    }
    wait_ticks = xhci_ms_to_ticks(timeout_ms);
    start = hal_timer_current_ticks();
    last = start;
    wait.kind = XHCI_WAIT_TRANSFER;
    wait.dev = dev;
    wait.slot_id = slot_id;
    wait.endpoint_id = endpoint_id;
    wait.expected_trb_phys = expected_trb_phys;
    wait.expected_index = expected_index;
    wait.expected_generation = expected_generation;
    wait.matched_slot_id = 0u;
    wait.completion = 0u;
    wait.residual = 0u;
    while ((uint32_t)(hal_timer_current_ticks() - start) < wait_ticks) {
        uint32_t now;

        if (dev != 0 && blockdev_request_cancelled(&dev->blockdev)) {
            return 0;
        }
        if (xhci_pump_event_for_wait(&wait)) {
            if (completion_out != 0) {
                *completion_out = wait.completion;
            }
            if (residual_out != 0) {
                *residual_out = wait.residual;
            }
            return 1;
        }
        now = hal_timer_current_ticks();
        if (now != last) {
            last = now;
            stagnant_spins = 0u;
        } else {
            stagnant_spins++;
            if (stagnant_spins >= XHCI_WAIT_SPINS_DEFAULT) {
                break;
            }
        }
        hal_cpu_relax();
    }
    return 0;
}

void xhci_drain_transfer_events(uint8_t slot_id, uint8_t endpoint_id, uint32_t max_events) {
    struct xhci_wait_request wait;
    uint32_t drained = 0u;

    if (slot_id == 0u || endpoint_id == 0u) {
        return;
    }
    xhci_forget_deferred_transfer_events(slot_id, endpoint_id);
    wait.kind = XHCI_WAIT_TRANSFER;
    wait.dev = 0;
    wait.slot_id = slot_id;
    wait.endpoint_id = endpoint_id;
    wait.expected_trb_phys = 0u;
    wait.expected_index = 0u;
    wait.expected_generation = 0u;
    wait.matched_slot_id = 0u;
    wait.completion = 0u;
    while (drained < max_events) {
        if (!xhci_pump_event_for_wait(&wait)) {
            break;
        }
        drained++;
        wait.completion = 0u;
    }
    xhci_forget_deferred_transfer_events(slot_id, endpoint_id);
}

static int xhci_command_enable_slot(uint8_t *slot_id_out) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;
    uint64_t command_trb_phys;

    if (!xhci_command_ready()) {
        return 0;
    }
    command_trb_phys = xhci_ring_command(0u, 0u, XHCI_TRB_ENABLE_SLOT << 10);
    if (!xhci_wait_command_completion(command_trb_phys, &slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS ||
        slot_id == 0u) {
        kprint("xhci: enable slot failed cc=%u slot=%u cmd=%lx\n",
               completion,
               (uint32_t)slot_id,
               command_trb_phys);
        return 0;
    }
    if (slot_id_out != 0) {
        *slot_id_out = slot_id;
    }
    return 1;
}

static void xhci_forget_slot_context(uint8_t slot_id) {
    if (slot_id == 0u || slot_id > g_xhci.max_slots) {
        return;
    }
    g_xhci.dcbaa[slot_id] = 0u;
}

static int xhci_command_disable_slot(uint8_t controller_index, uint8_t slot_id) {
    uint32_t completion = 0u;
    uint8_t event_slot_id = 0u;
    uint64_t command_trb_phys;

    if (slot_id == 0u) {
        return 1;
    }
    if (!xhci_select_controller(controller_index)) {
        return 0;
    }
    xhci_forget_slot_context(slot_id);
    if (!xhci_command_ready()) {
        xhci_save_active_controller();
        return 0;
    }
    command_trb_phys = xhci_ring_command(0u,
                                         0u,
                                         (XHCI_TRB_DISABLE_SLOT << 10) |
                                         ((uint32_t)slot_id << 24));
    if (!xhci_wait_command_completion(command_trb_phys, &event_slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: disable slot failed slot=%u cc=%u event_slot=%u\n",
               (uint32_t)slot_id,
               completion,
               (uint32_t)event_slot_id);
        xhci_save_active_controller();
        return 0;
    }
    xhci_save_active_controller();
    return 1;
}

static uint32_t xhci_deferred_slot_disable_retry_delay_ticks(void) {
    return xhci_ms_to_ticks(XHCI_DEFERRED_SLOT_DISABLE_RETRY_MS);
}

static int xhci_deferred_slot_disable_retry_ready(uint32_t now, uint32_t retry_after) {
    return retry_after == 0u || (int32_t)(now - retry_after) >= 0;
}

static void xhci_defer_disable_slot(uint8_t controller_index, uint8_t slot_id) {
    uint32_t now;

    if (slot_id == 0u ||
        controller_index >= XHCI_MAX_CONTROLLERS) {
        return;
    }
    now = hal_timer_current_ticks();
    for (uint32_t i = 0u; i < XHCI_DEFERRED_SLOT_DISABLES; i++) {
        struct xhci_deferred_slot_disable *entry = &g_xhci_deferred_slot_disables[i];

        if (!entry->used) {
            continue;
        }
        if (entry->controller_index == controller_index &&
            entry->slot_id == slot_id) {
            entry->retry_after = now + xhci_deferred_slot_disable_retry_delay_ticks();
            return;
        }
    }
    for (uint32_t i = 0u; i < XHCI_DEFERRED_SLOT_DISABLES; i++) {
        struct xhci_deferred_slot_disable *entry = &g_xhci_deferred_slot_disables[i];

        if (entry->used) {
            continue;
        }
        entry->used = 1u;
        entry->controller_index = controller_index;
        entry->slot_id = slot_id;
        entry->retry_logged = 0u;
        entry->retry_after = now + xhci_deferred_slot_disable_retry_delay_ticks();
        kprint("xhci: deferred disable slot%u controller=%u\n",
               (uint32_t)slot_id,
               (uint32_t)controller_index);
        return;
    }
    if (!g_xhci_deferred_slot_disable_drop_logged) {
        g_xhci_deferred_slot_disable_drop_logged = 1u;
        kprint("xhci: deferred disable slot queue full\n");
    }
}

void xhci_disable_device_slot(struct xhci_enum_device *dev) {
    uint8_t slot_id;

    if (dev == 0 || dev->slot_id == 0u) {
        return;
    }
    slot_id = dev->slot_id;
    if (!xhci_command_disable_slot(dev->controller_index, slot_id)) {
        xhci_defer_disable_slot(dev->controller_index, slot_id);
    }
}

void xhci_service_deferred_slot_disables(void) {
    uint32_t now;

    if (g_xhci_controller_count == 0u ||
        xhci_command_backoff_active()) {
        return;
    }
    now = hal_timer_current_ticks();
    for (uint32_t i = 0u; i < XHCI_DEFERRED_SLOT_DISABLES; i++) {
        struct xhci_deferred_slot_disable *entry = &g_xhci_deferred_slot_disables[i];

        if (!entry->used ||
            entry->controller_index != g_xhci_active_controller ||
            !xhci_deferred_slot_disable_retry_ready(now, entry->retry_after)) {
            continue;
        }
        if (xhci_command_disable_slot(entry->controller_index, entry->slot_id)) {
            kprint("xhci: deferred disable slot%u complete\n",
                   (uint32_t)entry->slot_id);
            memset(entry, 0, sizeof(*entry));
            now = hal_timer_current_ticks();
            continue;
        }
        entry->retry_after = hal_timer_current_ticks() +
                             xhci_deferred_slot_disable_retry_delay_ticks();
        if (!entry->retry_logged) {
            entry->retry_logged = 1u;
            kprint("xhci: deferred disable slot%u retry later\n",
                   (uint32_t)entry->slot_id);
        }
        if (xhci_command_backoff_active()) {
            break;
        }
        now = hal_timer_current_ticks();
    }
}

static int xhci_command_address_device(struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;
    uint64_t command_trb_phys;

    if (!xhci_command_ready()) {
        xhci_set_enum_failure(dev, "command-backoff");
        return 0;
    }
    command_trb_phys = xhci_ring_command(
        dev->input_context_phys,
        0u,
        (XHCI_TRB_ADDRESS_DEVICE << 10) |
        ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(command_trb_phys, &slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: address device failed port=%u slot=%u cc=%u event_slot=%u\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        xhci_set_enum_failure(dev, "address-device");
        return 0;
    }
    return 1;
}

int xhci_command_context(uint8_t command_type, struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;
    uint64_t command_trb_phys;

    if (!xhci_command_ready()) {
        xhci_set_enum_failure(dev, "command-backoff");
        return 0;
    }
    command_trb_phys = xhci_ring_command(dev->input_context_phys,
                                         0u,
                                         (command_type << 10) | ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(command_trb_phys, &slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: context command %u failed slot=%u cc=%u event_slot=%u\n",
               command_type,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        xhci_set_enum_failure(dev, "context-command");
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

static int xhci_speed_uses_tt(uint8_t speed) {
    return speed == XHCI_SPEED_FULL || speed == XHCI_SPEED_LOW;
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
    if (xhci_speed_uses_tt(dev->speed) && dev->tt_hub_slot_id != 0u) {
        slot_ctx[2] = ((uint32_t)dev->tt_hub_slot_id) |
                      ((uint32_t)dev->tt_port << 8) |
                      ((uint32_t)(dev->tt_think_time & 3u) << 16);
    }
    ep0_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (4u << 3) | (max_packet << 16);
    ep0_ctx[2] = (uint32_t)dev->ep0_ring_phys | 1u;
    ep0_ctx[3] = (uint32_t)(dev->ep0_ring_phys >> 32);
    ep0_ctx[4] = 8u;

    kprint("xhci: ADDRCTX slot=%u port=%u speed=%u route=%x "
       "parent_slot=%u parent_port=%u "
       "tt_hub_slot=%u tt_port=%u tt_think=%u ep0_mps=%u\n",
       (uint32_t)dev->slot_id,
       (uint32_t)dev->port,
       (uint32_t)dev->speed,
       dev->route_string,
       (uint32_t)dev->parent_slot_id,
       (uint32_t)dev->parent_port,
       (uint32_t)dev->tt_hub_slot_id,
       (uint32_t)dev->tt_port,
       (uint32_t)dev->tt_think_time,
       max_packet);

    kprint("xhci: ADDRCTX raw slot0=%x slot1=%x slot2=%x "
       "ep0_1=%x ep0_2=%x ep0_3=%x ep0_4=%x\n",
       slot_ctx[0],
       slot_ctx[1],
       slot_ctx[2],
       ep0_ctx[1],
       ep0_ctx[2],
       ep0_ctx[3],
       ep0_ctx[4]);
}

static uint64_t xhci_ep0_ring_trb(struct xhci_enum_device *dev,
                                  uint64_t parameter,
                                  uint32_t status,
                                  uint32_t control) {
    struct xhci_trb *trb = &dev->ep0_ring[dev->ep0_enqueue];
    uint64_t trb_phys = dev->ep0_ring_phys +
                        (uint64_t)dev->ep0_enqueue * XHCI_TRB_SIZE;

    xhci_ring_trb_set(trb, parameter, status, control | dev->ep0_cycle);
    dev->ep0_enqueue++;
    if (dev->ep0_enqueue >= XHCI_RING_TRBS - 1u) {
        dev->ep0_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (dev->ep0_cycle != 0u ? 1u : 0u);
        dev->ep0_cycle ^= 1u;
        dev->ep0_enqueue = 0u;
    }
    return trb_phys;
}

uint64_t xhci_transfer_ring_trb(struct xhci_trb *ring,
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
        if (XHCI_EVENT_TRACE != 0u) {
            kprint("xhci: transfer ring wrap phys=%lx next_cycle=%u\n",
                   ring_phys,
                   (uint32_t)(*cycle ^ 1u));
        }
        ring[XHCI_RING_TRBS - 1u].parameter_lo = (uint32_t)ring_phys;
        ring[XHCI_RING_TRBS - 1u].parameter_hi = (uint32_t)(ring_phys >> 32);
        ring[XHCI_RING_TRBS - 1u].status = 0u;
        ring[XHCI_RING_TRBS - 1u].control = (XHCI_TRB_LINK << 10) | 2u | (*cycle != 0u ? 1u : 0u);
        *cycle ^= 1u;
        *enqueue = 0u;
    }
    return trb_phys;
}

static void xhci_reset_transfer_ring_local(struct xhci_trb *ring,
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

uint8_t xhci_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    uint8_t *endpoint;
    uint32_t *ctx;

    if (dev == 0 || dev->device_context == 0 || endpoint_id == 0u) {
        return XHCI_EP_STATE_DISABLED;
    }
    endpoint = xhci_context_ptr(dev->device_context, endpoint_id);
    ctx = (uint32_t *)endpoint;
    return (uint8_t)(ctx[0] & 0x7u);
}

int xhci_recover_endpoint_ring(struct xhci_enum_device *dev,
                               uint8_t endpoint_id,
                               struct xhci_trb *ring,
                               uint64_t ring_phys,
                               uint32_t *enqueue,
                               uint8_t *cycle) {
    uint8_t state;

    if (dev == 0 || endpoint_id == 0u || ring == 0 ||
        ring_phys == 0u || enqueue == 0 || cycle == 0 ||
        !xhci_select_controller(dev->controller_index)) {
        return 0;
    }

    xhci_forget_deferred_transfer_events(dev->slot_id, endpoint_id);
    state = xhci_endpoint_state(dev, endpoint_id);
    if (state == XHCI_EP_STATE_RUNNING) {
        if (!xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u)) {
            xhci_save_active_controller();
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state == XHCI_EP_STATE_HALTED) {
        if (!xhci_command_endpoint(XHCI_TRB_RESET_ENDPOINT, dev, endpoint_id, 0u)) {
            xhci_save_active_controller();
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state != XHCI_EP_STATE_STOPPED &&
        state != XHCI_EP_STATE_ERROR &&
        state != XHCI_EP_STATE_DISABLED) {
        if (!xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u)) {
            xhci_save_active_controller();
            return 0;
        }
    }

    xhci_reset_transfer_ring_local(ring, ring_phys, enqueue, cycle);
    if (!xhci_command_endpoint(XHCI_TRB_SET_TR_DEQUEUE, dev, endpoint_id, ring_phys | 1u)) {
        xhci_save_active_controller();
        return 0;
    }
    xhci_drain_transfer_events(dev->slot_id, endpoint_id, 8u);
    xhci_save_active_controller();
    return 1;
}

static void xhci_reset_ep0_ring_after_control_failure(struct xhci_enum_device *dev) {
    if (dev == 0 || dev->ep0_ring == 0 || dev->ep0_ring_phys == 0u) {
        return;
    }
    xhci_forget_deferred_transfer_events(dev->slot_id, 1u);
    xhci_reset_transfer_ring_local(dev->ep0_ring,
                                   dev->ep0_ring_phys,
                                   &dev->ep0_enqueue,
                                   &dev->ep0_cycle);
    (void)xhci_command_endpoint(XHCI_TRB_SET_TR_DEQUEUE,
                                dev,
                                1u,
                                dev->ep0_ring_phys | 1u);
    xhci_drain_transfer_events(dev->slot_id, 1u, 8u);
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

int xhci_control_transfer(struct xhci_enum_device *dev,
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
    uint64_t status_trb_phys;
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
    status_trb_phys = xhci_ep0_ring_trb(dev,
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
                    (request_type == 0xa3u && request == USB_REQ_GET_STATUS) ||
                    (request_type == 0x21u &&
                     (request == USB_REQ_SET_IDLE || request == USB_REQ_SET_PROTOCOL));
    if (!xhci_wait_transfer_event_spins(dev->slot_id, 1u, &completion, status_trb_phys, wait_spins)) {
        xhci_reset_ep0_ring_after_control_failure(dev);
        xhci_set_enum_failure(dev, "control-timeout");
        if (!quiet_failure) {
            kprint("xhci: control timeout port=%u slot=%u req=%u type=%x\n",
                   (uint32_t)dev->port,
                   (uint32_t)dev->slot_id,
                   (uint32_t)request,
                   (uint32_t)request_type);
        }
        return 0;
    }
    if (completion != XHCI_CC_SUCCESS) {
        xhci_reset_ep0_ring_after_control_failure(dev);
        xhci_set_enum_failure(dev, "control-failed");
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

int xhci_control_set_configuration(struct xhci_enum_device *dev, uint8_t configuration) {
    return xhci_control_transfer(dev,
                                 0x00u,
                                 USB_REQ_SET_CONFIGURATION,
                                 configuration,
                                 0u,
                                 0,
                                 0u,
                                 0u);
}


int xhci_command_endpoint(uint8_t command_type,
                                 struct xhci_enum_device *dev,
                                 uint8_t endpoint_id,
                                 uint64_t parameter) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;
    uint64_t command_trb_phys;

    if (dev == 0 || endpoint_id == 0u) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    if (!xhci_command_ready()) {
        return 0;
    }
    command_trb_phys = xhci_ring_command(parameter,
                                         0u,
                                         ((uint32_t)command_type << 10) |
                                         ((uint32_t)endpoint_id << 16) |
                                         ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(command_trb_phys, &slot_id, &completion) ||
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


static int xhci_probe_descriptors(struct xhci_enum_device *dev) {
    uint8_t dev_desc[18];
    uint8_t cfg_head[9];
    uint8_t cfg[256];
    uint16_t total_len;

    memset(dev_desc, 0, sizeof(dev_desc));
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, 8u)) {
        xhci_set_enum_failure(dev, "device-desc8");
        kprint("xhci: slot%u device desc8 failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    if (!xhci_update_ep0_max_packet(dev, xhci_ep0_max_packet_from_descriptor(dev->speed, dev_desc[7]))) {
        xhci_set_enum_failure(dev, "ep0-mps");
        return 0;
    }
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, sizeof(dev_desc))) {
        xhci_set_enum_failure(dev, "device-desc18");
        kprint("xhci: slot%u device desc18 failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    XHCI_ENUM_TRACE("xhci: slot%u device desc class=%u subclass=%u proto=%u mps0=%u vendor=%x product=%x configs=%u\n",
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
        xhci_set_enum_failure(dev, "config-head");
        kprint("xhci: slot%u config head failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    total_len = usb_read_u16le(cfg_head + 2);
    if (total_len > sizeof(cfg)) {
        total_len = sizeof(cfg);
    }
    memset(cfg, 0, sizeof(cfg));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg, total_len)) {
        xhci_set_enum_failure(dev, "config-desc");
        return 0;
    }
    XHCI_ENUM_TRACE("xhci: slot%u config desc total=%u interfaces=%u config=%u attrs=%x max_power=%u\n",
                    (uint32_t)dev->slot_id,
                    (uint32_t)total_len,
                    (uint32_t)cfg[4],
                    (uint32_t)cfg[5],
                    (uint32_t)cfg[7],
                    (uint32_t)cfg[8]);
    if (xhci_parse_msc_config(dev, cfg, total_len)) {
        xhci_probe_msc(dev);
        return 1;
    }
    if (xhci_probe_hid_mouse(dev, cfg, total_len)) {
        return 1;
    }
    if (xhci_probe_hid_keyboard(dev, cfg, total_len)) {
        return 1;
    }
    if (dev_desc[4] == USB_CLASS_HUB || xhci_config_has_hub_interface(cfg, total_len)) {
        xhci_probe_hub(dev, cfg, total_len);
        return 1;
    }
    xhci_set_enum_failure(dev, "unsupported-class");
    return 0;
}


int xhci_enumerate_device(struct xhci_enum_device *dev) {
    uint8_t slot_id;

    if (dev == 0) {
        return 0;
    }
    dev->enum_failure = 0;
    if (!xhci_alloc_enum_device(dev)) {
        xhci_set_enum_failure(dev, "alloc-enum");
        xhci_release_failed_enum_device(dev);
        return 0;
    }
    if (!xhci_command_ready()) {
        xhci_set_enum_failure(dev, "command-backoff");
        xhci_release_failed_enum_device(dev);
        return 0;
    }
    if (!xhci_command_enable_slot(&dev->slot_id)) {
        xhci_set_enum_failure(dev, "enable-slot");
        xhci_release_failed_enum_device(dev);
        return 0;
    }
    g_xhci.dcbaa[dev->slot_id] = dev->device_context_phys;
    xhci_prepare_address_context(dev);

    if (dev->parent_slot_id != 0u) {
        kprint("xhci: ADDRESS child slot=%u parent=%u pport=%u "
            "root=%u route=%x speed=%u\n",
            (uint32_t)dev->slot_id,
            (uint32_t)dev->parent_slot_id,
            (uint32_t)dev->parent_port,
            (uint32_t)dev->port,
            dev->route_string,
            (uint32_t)dev->speed);
    }

    if (!xhci_command_address_device(dev)) {
        slot_id = dev->slot_id;
        if (slot_id != 0u) {
            if (!xhci_command_disable_slot(dev->controller_index, slot_id)) {
                xhci_defer_disable_slot(dev->controller_index, slot_id);
                kprint("xhci: failed enum slot%u detached in software only\n",
                       (uint32_t)slot_id);
            }
            dev->slot_id = 0u;
        }
        xhci_release_failed_enum_device(dev);
        return 0;
    }
    xhci_delay_ms(XHCI_ADDRESS_SETTLE_MS);
    dev->used = 1u;
    if (dev->parent_slot_id != 0u) {
        XHCI_ENUM_TRACE("xhci: enumerated hubslot=%u hubport=%u slot=%u rootport=%u route=%x speed=%u\n",
                        (uint32_t)dev->parent_slot_id,
                        (uint32_t)dev->parent_port,
                        (uint32_t)dev->slot_id,
                        (uint32_t)dev->port,
                        dev->route_string,
                        (uint32_t)dev->speed);
    } else {
        XHCI_ENUM_TRACE("xhci: enumerated port%u slot=%u speed=%u devctx=%x input=%x\n",
                        (uint32_t)dev->port,
                        (uint32_t)dev->slot_id,
                        (uint32_t)dev->speed,
                        (uint32_t)dev->device_context_phys,
                        (uint32_t)dev->input_context_phys);
    }
    (void)xhci_probe_descriptors(dev);
    return 1;
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
