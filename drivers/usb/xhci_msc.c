#include "drivers/usb/xhci_internal.h"

enum {
    XHCI_MSC_CONFIG_TRACE_ENABLED = 1u,
    XHCI_MSC_REENUM_RECOVERY_FAILURES = 8u,
    XHCI_MSC_LOCAL_RECOVERY_PASSES = 3u,
    XHCI_MSC_LOCAL_RECOVERY_SETTLE_MS = 100u,
    XHCI_MSC_READY_AFTER_RECOVERY_ATTEMPTS = 8u,
    XHCI_MSC_READY_AFTER_RECOVERY_DELAY_MS = 50u,
    XHCI_MSC_CSW_STALL_RETRY_LIMIT = 1u,
    XHCI_MSC_CSW_STALL_BOT_RESET_THRESHOLD = 16u,
    XHCI_MSC_SYNC_CACHE_STALL_SOFT_THRESHOLD = 4u,
    XHCI_MSC_SYNC_CACHE_SOFT_TICKS = 250u
};

static int xhci_msc_test_unit_ready_once(struct xhci_enum_device *dev);
static int xhci_msc_wait_ready_after_recovery(struct xhci_enum_device *dev);

static void xhci_msc_dma_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

#define XHCI_MSC_CONFIG_TRACE(...) do { \
    if (XHCI_MSC_CONFIG_TRACE_ENABLED) { \
        kprint(__VA_ARGS__); \
    } \
} while (0)

int xhci_parse_msc_config(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_msc = 0;
    uint8_t saw_msc = 0;
    uint8_t saw_uas = 0;
    uint8_t last_bulk_ep = 0;

    if (dev == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
    dev->msc_interface_number = 0u;
    dev->bulk_in_ep = 0u;
    dev->bulk_out_ep = 0u;
    dev->bulk_in_epid = 0u;
    dev->bulk_out_epid = 0u;
    dev->bulk_in_burst = 0u;
    dev->bulk_out_burst = 0u;
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
                XHCI_MSC_CONFIG_TRACE("xhci: slot%u MSC iface=%u subclass=%u proto=%x eps=%u bot=%u\n",
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
                last_bulk_ep = ep;
            } else if (attr == 2u && ep_num != 0u) {
                dev->bulk_out_ep = ep;
                dev->bulk_out_epid = (uint8_t)(ep_num * 2u);
                dev->bulk_out_mps = mps;
                last_bulk_ep = ep;
            }
        } else if (type == USB_DESC_SS_ENDPOINT_COMPANION && len >= 6u && in_msc && last_bulk_ep != 0u) {
            uint8_t burst = (uint8_t)(cfg[offset + 2u] & 0x0fu);

            if ((last_bulk_ep & 0x80u) != 0u) {
                dev->bulk_in_burst = burst;
            } else {
                dev->bulk_out_burst = burst;
            }
            XHCI_MSC_CONFIG_TRACE("xhci: slot%u MSC ss ep=%x burst=%u attr=%x bytes=%u\n",
                                  (uint32_t)dev->slot_id,
                                  (uint32_t)last_bulk_ep,
                                  (uint32_t)burst,
                                  (uint32_t)cfg[offset + 3u],
                                  (uint32_t)usb_read_u16le(cfg + offset + 4u));
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
    uint32_t out_avg = (uint32_t)dev->bulk_out_mps * ((uint32_t)dev->bulk_out_burst + 1u);
    uint32_t in_avg = (uint32_t)dev->bulk_in_mps * ((uint32_t)dev->bulk_in_burst + 1u);

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG |
            (1u << dev->bulk_out_epid) |
            (1u << dev->bulk_in_epid);
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << 27)) | ((uint32_t)last_epid << 27);

    if (out_avg > 0xffffu) {
        out_avg = 0xffffu;
    }
    if (in_avg > 0xffffu) {
        in_avg = 0xffffu;
    }

    out_ctx[1] = XHCI_EP_CONTEXT_CERR_3 |
                 (2u << 3) |
                 ((uint32_t)dev->bulk_out_burst << 8) |
                 ((uint32_t)dev->bulk_out_mps << 16);
    out_ctx[2] = (uint32_t)dev->bulk_out_ring_phys | 1u;
    out_ctx[3] = (uint32_t)(dev->bulk_out_ring_phys >> 32);
    out_ctx[4] = out_avg;

    in_ctx[1] = XHCI_EP_CONTEXT_CERR_3 |
                (6u << 3) |
                ((uint32_t)dev->bulk_in_burst << 8) |
                ((uint32_t)dev->bulk_in_mps << 16);
    in_ctx[2] = (uint32_t)dev->bulk_in_ring_phys | 1u;
    in_ctx[3] = (uint32_t)(dev->bulk_in_ring_phys >> 32);
    in_ctx[4] = in_avg;
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

static int xhci_dma_page_range_valid(uint64_t phys, uint32_t bytes) {
    uint64_t page_offset;

    if (phys == 0u || bytes == 0u || bytes > XHCI_PAGE_SIZE) {
        return 0;
    }
    page_offset = phys & (uint64_t)(XHCI_PAGE_SIZE - 1u);
    if ((uint64_t)bytes > (uint64_t)XHCI_PAGE_SIZE - page_offset) {
        return 0;
    }
    return 1;
}

static void xhci_msc_mark_offline(struct xhci_enum_device *dev, const char *reason);

static int xhci_msc_is_block_rw(uint8_t opcode) {
    return opcode == SCSI_READ_10 || opcode == SCSI_WRITE_10 ||
           opcode == SCSI_READ_16 || opcode == SCSI_WRITE_16;
}
static int xhci_msc_quarantine_ring(uint64_t ring_phys,
                                    uint8_t endpoint_id,
                                    uint32_t epoch);
static void xhci_msc_init_transfer_ring(struct xhci_trb *ring, uint64_t ring_phys);

static int xhci_reset_bulk_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    struct xhci_trb *new_ring = 0;
    struct xhci_trb *old_ring;
    uint64_t new_ring_phys = 0u;
    uint64_t old_ring_phys;
    uint32_t *enqueue;
    uint32_t old_epoch;
    uint32_t new_enqueue = 0u;
    uint8_t *cycle;
    uint8_t new_cycle = 1u;

    if (!xhci_bulk_endpoint_ring(dev, endpoint_id, &old_ring, &old_ring_phys, &enqueue, &cycle)) {
        return 0;
    }
    (void)old_ring;
    (void)enqueue;
    (void)cycle;
    if (!xhci_alloc_page(&new_ring_phys, (void **)&new_ring)) {
        return 0;
    }
    xhci_msc_init_transfer_ring(new_ring, new_ring_phys);

    if (!xhci_recover_endpoint_ring(dev,
                                    endpoint_id,
                                    new_ring,
                                    new_ring_phys,
                                    &new_enqueue,
                                    &new_cycle)) {
        (void)pmm_free_page(new_ring_phys);
        return 0;
    }

    if (endpoint_id == dev->bulk_in_epid) {
        old_epoch = dev->bulk_in_ring_epoch;
        dev->bulk_in_ring = new_ring;
        dev->bulk_in_ring_phys = new_ring_phys;
        dev->bulk_in_enqueue = new_enqueue;
        dev->bulk_in_cycle = new_cycle;
        dev->bulk_in_ring_epoch++;
        if (dev->bulk_in_ring_epoch == 0u) {
            dev->bulk_in_ring_epoch = 1u;
        }
        memset(dev->bulk_in_meta_generation, 0, sizeof(dev->bulk_in_meta_generation));
        memset(dev->bulk_in_meta_active, 0, sizeof(dev->bulk_in_meta_active));
        if (!xhci_msc_quarantine_ring(old_ring_phys, endpoint_id, old_epoch)) {
            xhci_msc_mark_offline(dev, "bulk-ring-quarantine-full");
            return 0;
        }
        return 1;
    }

    if (endpoint_id == dev->bulk_out_epid) {
        old_epoch = dev->bulk_out_ring_epoch;
        dev->bulk_out_ring = new_ring;
        dev->bulk_out_ring_phys = new_ring_phys;
        dev->bulk_out_enqueue = new_enqueue;
        dev->bulk_out_cycle = new_cycle;
        dev->bulk_out_ring_epoch++;
        if (dev->bulk_out_ring_epoch == 0u) {
            dev->bulk_out_ring_epoch = 1u;
        }
        memset(dev->bulk_out_meta_generation, 0, sizeof(dev->bulk_out_meta_generation));
        memset(dev->bulk_out_meta_active, 0, sizeof(dev->bulk_out_meta_active));
        if (!xhci_msc_quarantine_ring(old_ring_phys, endpoint_id, old_epoch)) {
            xhci_msc_mark_offline(dev, "bulk-ring-quarantine-full");
            return 0;
        }
        return 1;
    }

    (void)pmm_free_page(new_ring_phys);
    return 0;
}

static void xhci_msc_mark_offline(struct xhci_enum_device *dev, const char *reason) {
    if (dev == 0) {
        return;
    }
    dev->msc_offline = blockdev_is_rootfs_protected(&dev->blockdev) ? 0u : 1u;
    blockdev_record_failure(&dev->blockdev, -1, reason);
    (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_OFFLINE);
    dev->read_cache_valid = 0u;
    if (dev->msc_transport_failure_logged == 0u) {
        dev->msc_transport_failure_logged = 1u;
        kprint("xhci: MSC slot=%u %s reason=%s\n",
               (uint32_t)dev->slot_id,
               dev->msc_offline ? "offline" : "rootfs-protected",
               reason != 0 ? reason : "transport-error");
    }
}

struct xhci_msc_request_dma {
    uint64_t cbw_phys;
    uint64_t data_phys;
    uint64_t csw_phys;
    uint8_t *cbw;
    uint8_t *data;
    uint8_t *csw;
    uint32_t generation;
};

struct xhci_msc_quarantined_dma {
    uint64_t cbw_phys;
    uint64_t data_phys;
    uint64_t csw_phys;
    uint32_t generation;
};

struct xhci_msc_quarantined_ring {
    uint64_t ring_phys;
    uint32_t epoch;
    uint8_t endpoint_id;
};

enum {
    XHCI_MSC_QUARANTINE_MAX = 64u,
    XHCI_MSC_RING_QUARANTINE_MAX = 64u
};

static struct xhci_msc_quarantined_dma g_xhci_msc_quarantine[XHCI_MSC_QUARANTINE_MAX];
static uint32_t g_xhci_msc_quarantine_count;
static struct xhci_msc_quarantined_ring g_xhci_msc_ring_quarantine[XHCI_MSC_RING_QUARANTINE_MAX];
static uint32_t g_xhci_msc_ring_quarantine_count;

static void xhci_msc_dma_guard_fill(uint8_t *page) {
    if (page == 0) {
        return;
    }
    memset(page, 0xa5, XHCI_MSC_DMA_GUARD_SIZE);
    memset(page + XHCI_PAGE_SIZE - XHCI_MSC_DMA_GUARD_SIZE,
           0x5a,
           XHCI_MSC_DMA_GUARD_SIZE);
}

static int xhci_msc_dma_guard_ok(const uint8_t *page) {
    if (page == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < XHCI_MSC_DMA_GUARD_SIZE; i++) {
        if (page[i] != 0xa5u) {
            return 0;
        }
    }
    for (uint32_t i = 0u; i < XHCI_MSC_DMA_GUARD_SIZE; i++) {
        if (page[XHCI_PAGE_SIZE - XHCI_MSC_DMA_GUARD_SIZE + i] != 0x5au) {
            return 0;
        }
    }
    return 1;
}

static int xhci_msc_request_dma_alloc(struct xhci_enum_device *dev,
                                      struct xhci_msc_request_dma *req) {
    if (dev == 0 || req == 0) {
        return 0;
    }
    memset(req, 0, sizeof(*req));
    if (!xhci_alloc_page(&req->cbw_phys, (void **)&req->cbw) ||
        !xhci_alloc_page(&req->data_phys, (void **)&req->data) ||
        !xhci_alloc_page(&req->csw_phys, (void **)&req->csw)) {
        if (req->cbw_phys != 0u) {
            (void)pmm_free_page(req->cbw_phys);
        }
        if (req->data_phys != 0u) {
            (void)pmm_free_page(req->data_phys);
        }
        if (req->csw_phys != 0u) {
            (void)pmm_free_page(req->csw_phys);
        }
        memset(req, 0, sizeof(*req));
        return 0;
    }
    if (req->cbw_phys == req->data_phys ||
        req->cbw_phys == req->csw_phys ||
        req->data_phys == req->csw_phys ||
        req->cbw == req->data ||
        req->cbw == req->csw ||
        req->data == req->csw) {
        kprint("xhci: MSC request DMA alias cbw=%lx data=%lx csw=%lx\n",
               req->cbw_phys,
               req->data_phys,
               req->csw_phys);
        if (req->cbw_phys != 0u) {
            (void)pmm_free_page(req->cbw_phys);
        }
        if (req->data_phys != 0u && req->data_phys != req->cbw_phys) {
            (void)pmm_free_page(req->data_phys);
        }
        if (req->csw_phys != 0u &&
            req->csw_phys != req->cbw_phys &&
            req->csw_phys != req->data_phys) {
            (void)pmm_free_page(req->csw_phys);
        }
        memset(req, 0, sizeof(*req));
        return 0;
    }
    req->generation = ++dev->msc_next_generation;
    if (req->generation == 0u) {
        req->generation = ++dev->msc_next_generation;
    }
    memset(req->cbw, 0, XHCI_PAGE_SIZE);
    memset(req->data, 0, XHCI_PAGE_SIZE);
    memset(req->csw, 0, XHCI_PAGE_SIZE);
    xhci_msc_dma_guard_fill(req->data);
    return 1;
}

static void xhci_msc_request_dma_free(struct xhci_msc_request_dma *req) {
    if (req == 0) {
        return;
    }
    if (req->cbw_phys != 0u) {
        (void)pmm_free_page(req->cbw_phys);
    }
    if (req->data_phys != 0u) {
        (void)pmm_free_page(req->data_phys);
    }
    if (req->csw_phys != 0u) {
        (void)pmm_free_page(req->csw_phys);
    }
    memset(req, 0, sizeof(*req));
}

static int xhci_msc_request_dma_quarantine(struct xhci_msc_request_dma *req) {
    static uint8_t logged = 0u;
    static uint8_t full_logged = 0u;

    if (req == 0) {
        return 1;
    }
    if (req->cbw_phys == 0u && req->data_phys == 0u && req->csw_phys == 0u) {
        memset(req, 0, sizeof(*req));
        return 1;
    }
    if (g_xhci_msc_quarantine_count >= XHCI_MSC_QUARANTINE_MAX) {
        if (!full_logged) {
            full_logged = 1u;
            kprint("xhci: MSC DMA quarantine full, keeping request pages leaked\n");
        }
        memset(req, 0, sizeof(*req));
        return 0;
    }
    g_xhci_msc_quarantine[g_xhci_msc_quarantine_count].cbw_phys = req->cbw_phys;
    g_xhci_msc_quarantine[g_xhci_msc_quarantine_count].data_phys = req->data_phys;
    g_xhci_msc_quarantine[g_xhci_msc_quarantine_count].csw_phys = req->csw_phys;
    g_xhci_msc_quarantine[g_xhci_msc_quarantine_count].generation = req->generation;
    g_xhci_msc_quarantine_count++;
    if (!logged) {
        logged = 1u;
        kprint("xhci: MSC quarantining failed request DMA pages\n");
    }
    memset(req, 0, sizeof(*req));
    return 1;
}

static uint64_t xhci_msc_data_payload_phys(const struct xhci_msc_request_dma *req) {
    return req->data_phys + XHCI_MSC_DMA_GUARD_SIZE;
}

static uint8_t *xhci_msc_data_payload(struct xhci_msc_request_dma *req) {
    return req->data + XHCI_MSC_DMA_GUARD_SIZE;
}

static int xhci_msc_phys_ranges_overlap(uint64_t a, uint32_t a_len, uint64_t b, uint32_t b_len) {
    uint64_t a_end;
    uint64_t b_end;

    if (a == 0u || b == 0u || a_len == 0u || b_len == 0u) {
        return 0;
    }
    a_end = a + a_len;
    b_end = b + b_len;
    if (a_end < a || b_end < b) {
        return 1;
    }
    return a < b_end && b < a_end;
}

static int xhci_msc_request_dma_layout_ok(const struct xhci_msc_request_dma *req,
                                          uint32_t data_len,
                                          uint8_t op) {
    uint64_t data_payload_phys;

    if (req == 0) {
        return 0;
    }
    data_payload_phys = xhci_msc_data_payload_phys(req);
    if (xhci_msc_phys_ranges_overlap(req->cbw_phys, 31u, req->csw_phys, 13u) ||
        xhci_msc_phys_ranges_overlap(req->cbw_phys, 31u, data_payload_phys, data_len) ||
        xhci_msc_phys_ranges_overlap(data_payload_phys, data_len, req->csw_phys, 13u)) {
        kprint("xhci: MSC DMA overlap op=%x cbw=%lx/31 data=%lx/%u csw=%lx/13 gen=%u\n",
               (uint32_t)op,
               req->cbw_phys,
               data_payload_phys,
               data_len,
               req->csw_phys,
               req->generation);
        return 0;
    }
    return 1;
}

static int xhci_msc_csw_pattern_unchanged(const struct xhci_msc_request_dma *req) {
    if (req == 0 || req->csw == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < 13u; i++) {
        if (req->csw[i] != 0xccu) {
            return 0;
        }
    }
    return 1;
}

static void xhci_msc_check_csw_submission(struct xhci_enum_device *dev,
                                          const struct xhci_msc_request_dma *req,
                                          uint8_t op,
                                          uint32_t expected_actual) {
    uint64_t expected_trb_phys;

    if (dev == 0 || req == 0) {
        return;
    }
    expected_trb_phys = dev->bulk_in_ring_phys +
                        (uint64_t)dev->last_bulk_index * XHCI_TRB_SIZE;
    if (dev->last_bulk_epid != dev->bulk_in_epid ||
        (dev->last_bulk_buffer_phys & ~0xfull) != (req->csw_phys & ~0xfull) ||
        (dev->last_bulk_trb_parameter & ~0xfull) != (req->csw_phys & ~0xfull) ||
        (dev->last_bulk_trb_phys & ~0xfull) != (expected_trb_phys & ~0xfull)) {
        kprint("xhci: MSC CSW submit mismatch slot=%u op=%x epid=%u/%u trb=%lx/%lx buffer=%lx param=%lx csw=%lx idx=%u gen=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)op,
               (uint32_t)dev->last_bulk_epid,
               (uint32_t)dev->bulk_in_epid,
               dev->last_bulk_trb_phys,
               expected_trb_phys,
               dev->last_bulk_buffer_phys,
               dev->last_bulk_trb_parameter,
               req->csw_phys,
               dev->last_bulk_index,
               dev->last_bulk_generation);
    }
    if (expected_actual != 0xffffffffu && dev->last_bulk_actual != expected_actual) {
        kprint("xhci: MSC CSW actual mismatch slot=%u op=%x req=%u actual=%u residual=%u cc=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)op,
               dev->last_bulk_requested,
               dev->last_bulk_actual,
               dev->last_bulk_residual,
               (uint32_t)dev->last_bulk_completion);
    }
}

static void xhci_msc_log_csw_stall_retry(struct xhci_enum_device *dev,
                                         uint32_t tag,
                                         uint8_t op,
                                         uint32_t residual) {
    static uint32_t logged = 0u;
    static uint32_t suppressed = 0u;

    logged++;
    if (logged <= 8u || (logged & 0x3fu) == 0u) {
        kprint("xhci: MSC CSW stalled, clear halt and retry slot=%u tag=%x op=%x residual=%u suppressed=%u\n",
               (uint32_t)(dev != 0 ? dev->slot_id : 0u),
               tag,
               (uint32_t)op,
               residual,
               suppressed);
        suppressed = 0u;
        return;
    }
    suppressed++;
}

static void xhci_msc_note_csw_stall(struct xhci_enum_device *dev, uint8_t op) {
    if (dev == 0) {
        return;
    }
    dev->last_msc_csw_stalled = 1u;
    if (dev->msc_csw_stall_count < 0xffu) {
        dev->msc_csw_stall_count++;
    }
    if (dev->msc_csw_stall_count >= XHCI_MSC_CSW_STALL_BOT_RESET_THRESHOLD) {
        dev->msc_csw_stall_reset_due = 1u;
    }
    if (op == SCSI_SYNCHRONIZE_CACHE_10) {
        if (dev->msc_sync_cache_stall_count < 0xffu) {
            dev->msc_sync_cache_stall_count++;
        }
        if (dev->msc_sync_cache_stall_count >= XHCI_MSC_SYNC_CACHE_STALL_SOFT_THRESHOLD) {
            dev->msc_sync_cache_soft_until =
                hal_timer_current_ticks() + XHCI_MSC_SYNC_CACHE_SOFT_TICKS;
            dev->msc_sync_cache_stall_count = 0u;
            kprint("xhci: MSC sync-cache stall quirk active slot=%u ticks=%u\n",
                   (uint32_t)dev->slot_id,
                   XHCI_MSC_SYNC_CACHE_SOFT_TICKS);
        }
    } else {
        dev->msc_sync_cache_stall_count = 0u;
    }
}

static void xhci_msc_note_clean_csw(struct xhci_enum_device *dev, uint8_t op) {
    if (dev == 0 || dev->last_msc_csw_stalled != 0u) {
        return;
    }
    if (dev->msc_csw_stall_count != 0u) {
        dev->msc_csw_stall_count--;
    }
    if (op == SCSI_SYNCHRONIZE_CACHE_10 && dev->msc_sync_cache_stall_count != 0u) {
        dev->msc_sync_cache_stall_count--;
    }
}

int xhci_msc_sync_cache_soft_allowed(struct xhci_enum_device *dev) {
    if (dev == 0 || dev->msc_sync_cache_soft_until == 0u) {
        return 0;
    }
    if ((int32_t)(hal_timer_current_ticks() - dev->msc_sync_cache_soft_until) < 0) {
        return 1;
    }
    dev->msc_sync_cache_soft_until = 0u;
    return 0;
}

static int xhci_msc_quarantine_ring(uint64_t ring_phys,
                                    uint8_t endpoint_id,
                                    uint32_t epoch) {
    static uint8_t logged = 0u;
    static uint8_t full_logged = 0u;

    if (ring_phys == 0u) {
        return 1;
    }
    if (g_xhci_msc_ring_quarantine_count >= XHCI_MSC_RING_QUARANTINE_MAX) {
        if (!full_logged) {
            full_logged = 1u;
            kprint("xhci: MSC bulk ring quarantine full, keeping old ring leaked\n");
        }
        return 0;
    }
    g_xhci_msc_ring_quarantine[g_xhci_msc_ring_quarantine_count].ring_phys = ring_phys;
    g_xhci_msc_ring_quarantine[g_xhci_msc_ring_quarantine_count].endpoint_id = endpoint_id;
    g_xhci_msc_ring_quarantine[g_xhci_msc_ring_quarantine_count].epoch = epoch;
    g_xhci_msc_ring_quarantine_count++;
    if (!logged) {
        logged = 1u;
        kprint("xhci: MSC quarantining recovered bulk rings\n");
    }
    return 1;
}

static void xhci_msc_init_transfer_ring(struct xhci_trb *ring, uint64_t ring_phys) {
    if (ring == 0 || ring_phys == 0u) {
        return;
    }
    memset(ring, 0, XHCI_PAGE_SIZE);
    xhci_ring_trb_set(&ring[XHCI_RING_TRBS - 1u],
                      ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
}

static void xhci_bulk_note_submission(struct xhci_enum_device *dev,
                                      uint8_t endpoint_id,
                                      uint32_t index,
                                      uint32_t generation) {
    uint32_t *generations;
    uint8_t *active;

    if (dev == 0 || index >= XHCI_RING_TRBS) {
        return;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        generations = dev->bulk_in_meta_generation;
        active = dev->bulk_in_meta_active;
    } else if (endpoint_id == dev->bulk_out_epid) {
        generations = dev->bulk_out_meta_generation;
        active = dev->bulk_out_meta_active;
    } else {
        return;
    }
    generations[index] = generation;
    active[index] = 1u;
}

static void xhci_bulk_note_completion(struct xhci_enum_device *dev,
                                      uint8_t endpoint_id,
                                      uint32_t index,
                                      uint32_t generation) {
    uint32_t *generations;
    uint8_t *active;

    if (dev == 0 || index >= XHCI_RING_TRBS) {
        return;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        generations = dev->bulk_in_meta_generation;
        active = dev->bulk_in_meta_active;
    } else if (endpoint_id == dev->bulk_out_epid) {
        generations = dev->bulk_out_meta_generation;
        active = dev->bulk_out_meta_active;
    } else {
        return;
    }
    if (active[index] != 0u && generations[index] != generation) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: bulk generation mismatch slot=%u ep=%u index=%u got=%u want=%u\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)endpoint_id,
                                 index,
                                 generations[index],
                                 generation);
    }
    active[index] = 0u;
}

static int xhci_bulk_transfer(struct xhci_enum_device *dev,
                              uint8_t endpoint_id,
                              uint64_t phys,
                              uint32_t bytes,
                              uint32_t *actual_length_out,
                              uint32_t timeout_ms) {
    uint32_t completion = 0u;
    uint32_t residual = 0u;
    struct xhci_trb *ring;
    uint64_t ring_phys;
    uint64_t submitted_trb_phys;
    uint32_t trb_flags;
    uint32_t *enqueue;
    uint32_t submitted_index;
    uint32_t generation;
    uint8_t *cycle;

    if (dev == 0 || dev->msc_offline || endpoint_id == 0u ||
        !xhci_dma_page_range_valid(phys, bytes)) {
        return 0;
    }
    if (actual_length_out != 0) {
        *actual_length_out = 0u;
    }
    dev->last_bulk_completion = 0u;
    dev->last_bulk_epid = endpoint_id;
    dev->last_bulk_index = 0u;
    dev->last_bulk_generation = 0u;
    dev->last_bulk_requested = bytes;
    dev->last_bulk_actual = 0u;
    dev->last_bulk_residual = 0u;
    dev->last_bulk_trb_phys = 0u;
    dev->last_bulk_buffer_phys = phys;
    dev->last_bulk_trb_parameter = 0u;
    dev->last_bulk_trb_status = 0u;
    dev->last_bulk_trb_control = 0u;
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
    submitted_index = *enqueue;
    generation = ++dev->msc_next_generation;
    if (generation == 0u) {
        generation = ++dev->msc_next_generation;
    }
    xhci_bulk_note_submission(dev, endpoint_id, submitted_index, generation);
    dev->last_bulk_index = submitted_index;
    dev->last_bulk_generation = generation;
    submitted_trb_phys = xhci_transfer_ring_trb(ring,
                                                ring_phys,
                                                enqueue,
                                                cycle,
                                                phys,
                                                bytes,
                                                trb_flags);
    dev->last_bulk_trb_phys = submitted_trb_phys;
    dev->last_bulk_trb_parameter =
        ((uint64_t)ring[submitted_index].parameter_hi << 32) |
        ring[submitted_index].parameter_lo;
    dev->last_bulk_trb_status = ring[submitted_index].status;
    dev->last_bulk_trb_control = ring[submitted_index].control;
    if ((dev->last_bulk_trb_parameter & ~0xfull) != (phys & ~0xfull)) {
        kprint("xhci: bulk TRB parameter mismatch slot=%u epid=%u idx=%u want=%lx got=%lx status=%x control=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               submitted_index,
               phys,
               dev->last_bulk_trb_parameter,
               dev->last_bulk_trb_status,
               dev->last_bulk_trb_control);
        xhci_bulk_note_completion(dev, endpoint_id, submitted_index, generation);
        xhci_save_active_controller();
        return 0;
    }
    xhci_msc_dma_barrier();
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, endpoint_id);
    if (!xhci_wait_transfer_event_generation_ms(dev,
                                                dev->slot_id,
                                                endpoint_id,
                                                &completion,
                                                &residual,
                                                submitted_trb_phys,
                                                submitted_index,
                                                generation,
                                                timeout_ms)) {
        uint8_t state = xhci_endpoint_state(dev, endpoint_id);
        dev->last_bulk_completion = 0u;
        dev->last_bulk_actual = 0u;
        dev->last_bulk_residual = bytes;
        XHCI_MSC_TRANSPORT_TRACE("xhci: bulk %s slot=%u epid=%u bytes=%u trb=%lx idx=%u gen=%u state=%u enq=%u cyc=%u ev=%u/%u usbsts=%x\n",
                                 blockdev_request_cancelled(&dev->blockdev) ? "cancelled/timeout" : "timeout",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)endpoint_id,
                                 bytes,
                                 submitted_trb_phys,
                                 submitted_index,
                                 generation,
                                 (uint32_t)state,
                                 *enqueue,
                                 (uint32_t)*cycle,
                                 g_xhci.event_dequeue,
                                 (uint32_t)g_xhci.event_cycle,
                                 xhci_read32(g_xhci.op, XHCI_OP_USBSTS));
        xhci_bulk_note_completion(dev, endpoint_id, submitted_index, generation);
        xhci_save_active_controller();
        return 0;
    }
    if (residual > bytes) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: bulk invalid residual slot=%u epid=%u idx=%u gen=%u residual=%u bytes=%u\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)endpoint_id,
                                 submitted_index,
                                 generation,
                                 residual,
                                 bytes);
        dev->last_bulk_completion = (uint8_t)completion;
        dev->last_bulk_actual = 0u;
        dev->last_bulk_residual = residual;
        xhci_bulk_note_completion(dev, endpoint_id, submitted_index, generation);
        xhci_save_active_controller();
        return 0;
    }
    dev->last_bulk_completion = (uint8_t)completion;
    dev->last_bulk_actual = bytes - residual;
    dev->last_bulk_residual = residual;
    if (dev->last_bulk_actual + dev->last_bulk_residual != bytes) {
        kprint("xhci: bulk length accounting mismatch slot=%u epid=%u idx=%u gen=%u req=%u actual=%u residual=%u cc=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               submitted_index,
               generation,
               bytes,
               dev->last_bulk_actual,
               dev->last_bulk_residual,
               completion);
    }
    if (completion == XHCI_CC_SUCCESS && residual == bytes && bytes != 0u) {
        kprint("xhci: bulk success-with-no-data slot=%u epid=%u idx=%u gen=%u req=%u trb=%lx buffer=%lx\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               submitted_index,
               generation,
               bytes,
               submitted_trb_phys,
               phys);
    }
    if (completion != XHCI_CC_SUCCESS &&
        !(completion == XHCI_CC_SHORT_PACKET && (endpoint_id & 1u) != 0u)) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: bulk failed slot=%u epid=%u idx=%u gen=%u cc=%u residual=%u bytes=%u\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)endpoint_id,
                                 submitted_index,
                                 generation,
                                 completion,
                                 residual,
                                 bytes);
        xhci_bulk_note_completion(dev, endpoint_id, submitted_index, generation);
        xhci_save_active_controller();
        return 0;
    }
    if (actual_length_out != 0) {
        *actual_length_out = bytes - residual;
    }
    xhci_bulk_note_completion(dev, endpoint_id, submitted_index, generation);
    xhci_save_active_controller();
    return 1;
}

static uint32_t xhci_msc_request_deadline_ms(uint8_t op) {
    if (op == SCSI_WRITE_10 || op == SCSI_WRITE_16) {
        return 10000u;
    }
    if (op == SCSI_SYNCHRONIZE_CACHE_10) {
        return 15000u;
    }
    if (op == SCSI_READ_10 || op == SCSI_READ_16) {
        return 5000u;
    }
    return 3000u;
}

static uint32_t xhci_msc_data_timeout_ms(uint8_t op, uint8_t data_in) {
    (void)data_in;
    if (op == SCSI_WRITE_10 || op == SCSI_WRITE_16) {
        return 10000u;
    }
    if (op == SCSI_READ_10 || op == SCSI_READ_16) {
        return 5000u;
    }
    return 3000u;
}

static uint32_t xhci_msc_status_timeout_ms(uint8_t op) {
    if (op == SCSI_WRITE_10 || op == SCSI_WRITE_16 ||
        op == SCSI_SYNCHRONIZE_CACHE_10) {
        return 15000u;
    }
    return 3000u;
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
    if (dev == 0 || dev->msc_offline) {
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

static void xhci_msc_note_transport_recovery_failed(struct xhci_enum_device *dev,
                                                    uint8_t op,
                                                    const char *phase) {
    if (dev == 0) {
        return;
    }
    dev->read_cache_valid = 0u;
    if (dev->msc_transport_failure_logged == 0u) {
        dev->msc_transport_failure_logged = 1u;
        kprint("xhci: MSC recovery failed slot=%u op=%x phase=%s\n",
               (uint32_t)dev->slot_id,
               (uint32_t)op,
               phase != 0 ? phase : "?");
    }
    if (!dev->msc_offline) {
        (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_ONLINE);
    }
}

static int xhci_msc_reenumerate_recovery(struct xhci_enum_device *dev,
                                         uint8_t op,
                                         const char *phase) {
    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    if (xhci_reenumerate_and_rebind_blockdev(dev)) {
        blockdev_record_rebind(&dev->blockdev, phase);
        if (!xhci_msc_wait_ready_after_recovery(dev)) {
            xhci_msc_note_transport_recovery_failed(dev, op, "rebind-test-unit-ready");
            return 0;
        }
        (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_ONLINE);
        dev->msc_transport_failure_logged = 0u;
        return 1;
    }
    xhci_msc_note_transport_recovery_failed(dev, op, phase);
    return 0;
}

static uint8_t xhci_msc_data_endpoint_for_op(struct xhci_enum_device *dev, uint8_t op) {
    if (dev == 0) {
        return 0u;
    }
    if (op == SCSI_READ_10 || op == SCSI_READ_16) {
        return dev->bulk_in_epid;
    }
    if (op == SCSI_WRITE_10 || op == SCSI_WRITE_16) {
        return dev->bulk_out_epid;
    }
    return 0u;
}

static int xhci_msc_reset_endpoint_for_phase(struct xhci_enum_device *dev,
                                             uint8_t op,
                                             uint8_t failed_phase) {
    uint8_t epid;

    if (dev == 0) {
        return 0;
    }
    if (failed_phase == 1u) {
        return xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid);
    }
    if (failed_phase == 2u) {
        epid = xhci_msc_data_endpoint_for_op(dev, op);
        if (epid == 0u) {
            return xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid) &&
                   xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid);
        }
        return xhci_reset_bulk_endpoint_state(dev, epid);
    }
    if (failed_phase == 3u) {
        return xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid);
    }
    return xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid) &&
           xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid);
}

static int xhci_msc_standard_recover_transport(struct xhci_enum_device *dev,
                                               uint8_t op,
                                               const char *phase,
                                               uint8_t failed_phase) {
    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    if (!xhci_recover_parent_hub_port(dev)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "hub-port-reset");
        return 0;
    }
    if (!xhci_msc_bulk_only_reset(dev)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "bot-reset");
        return 0;
    }
    xhci_delay_ms(XHCI_MSC_RESET_SETTLE_MS);
    if (!xhci_clear_endpoint_halt(dev, dev->bulk_in_ep)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "clear-in");
        return 0;
    }
    xhci_delay_ms(20u);
    if (!xhci_clear_endpoint_halt(dev, dev->bulk_out_ep)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "clear-out");
        return 0;
    }
    if (!xhci_msc_reset_endpoint_for_phase(dev, op, failed_phase)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "endpoint-rearm-phase");
        return 0;
    }
    if (failed_phase == 3u &&
        !xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid)) {
        xhci_msc_note_transport_recovery_failed(dev, op, "endpoint-rearm");
        return 0;
    }
    xhci_drain_transfer_events(dev->slot_id, dev->bulk_in_epid, 32u);
    xhci_drain_transfer_events(dev->slot_id, dev->bulk_out_epid, 32u);
    xhci_delay_ms(50u);
    (void)phase;
    return 1;
}

static int xhci_msc_test_unit_ready_once(struct xhci_enum_device *dev) {
    uint8_t cmd[6];

    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return xhci_msc_command(dev, cmd, 6u, 0, 0u, 0u);
}

static int xhci_msc_wait_ready_after_recovery(struct xhci_enum_device *dev) {
    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < XHCI_MSC_READY_AFTER_RECOVERY_ATTEMPTS; attempt++) {
        if (xhci_msc_test_unit_ready_once(dev)) {
            return 1;
        }
        if (dev->msc_offline) {
            return 0;
        }
        if (dev->last_msc_status == MSC_STATUS_TRANSPORT_ERROR) {
            uint8_t failed_phase = dev->last_msc_phase;

            kprint("xhci: MSC ready TUR transport retry attempt=%u phase=%u\n",
                   attempt + 1u,
                   (uint32_t)failed_phase);
            if (!xhci_msc_standard_recover_transport(dev,
                                                     SCSI_TEST_UNIT_READY,
                                                     "ready-tur",
                                                     failed_phase)) {
                return 0;
            }
        }
        xhci_delay_ms(XHCI_MSC_READY_AFTER_RECOVERY_DELAY_MS);
    }
    return 0;
}

static int xhci_msc_recover_transport_local(struct xhci_enum_device *dev,
                                            uint8_t op,
                                            const char *phase,
                                            uint8_t failed_phase) {
    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    for (uint32_t pass = 0u; pass < XHCI_MSC_LOCAL_RECOVERY_PASSES; pass++) {
        if (pass != 0u) {
            kprint("xhci: MSC local recovery retry slot=%u op=%x phase=%s pass=%u\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)op,
                   phase != 0 ? phase : "?",
                   pass + 1u);
        }
        if (!xhci_msc_standard_recover_transport(dev, op, phase, failed_phase)) {
            xhci_delay_ms(XHCI_MSC_LOCAL_RECOVERY_SETTLE_MS);
            continue;
        }
        if (xhci_msc_wait_ready_after_recovery(dev)) {
            return 1;
        }
        xhci_delay_ms(XHCI_MSC_LOCAL_RECOVERY_SETTLE_MS);
    }
    return 0;
}

static int xhci_msc_recover_transport(struct xhci_enum_device *dev,
                                      uint8_t op,
                                      const char *phase,
                                      uint8_t allow_reenumerate) {
    uint8_t failed_phase;

    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    failed_phase = dev->last_msc_phase;
    (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_RECOVERING);
    XHCI_MSC_TRANSPORT_TRACE("xhci: MSC recovery slot=%u op=%x phase=%s in=%x out=%x\n",
                             (uint32_t)dev->slot_id,
                             (uint32_t)op,
                             phase,
                             (uint32_t)dev->bulk_in_ep,
                             (uint32_t)dev->bulk_out_ep);
    if (!xhci_msc_recover_transport_local(dev, op, phase, failed_phase)) {
        dev->msc_recovery_failure_count++;
        if (allow_reenumerate &&
            dev->msc_recovery_failure_count >= XHCI_MSC_REENUM_RECOVERY_FAILURES) {
            dev->msc_recovery_failure_count = 0u;
            return xhci_msc_reenumerate_recovery(dev, op, phase);
        }
        return 0;
    }
    (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_ONLINE);
    dev->msc_transport_failure_logged = 0u;
    dev->msc_recovery_failure_count = 0u;
    dev->msc_csw_stall_count = 0u;
    dev->msc_csw_stall_reset_due = 0u;
    dev->msc_sync_cache_stall_count = 0u;
    dev->msc_sync_cache_soft_until = 0u;
    return 1;
}

int xhci_msc_reset_block_device(struct block_device *bdev) {
    struct xhci_enum_device *dev = bdev != 0
        ? (struct xhci_enum_device *)bdev->driver_data
        : 0;

    if (dev == 0) {
        return -1;
    }
    dev->msc_offline = 0u;
    return xhci_msc_recover_transport(dev, 0u, "blockdev-reset", 1u) ? 0 : -1;
}

int xhci_msc_rebind_blockdev(struct xhci_enum_device *old_dev,
                             struct xhci_enum_device *fresh_dev) {
    struct block_device saved_blockdev;
    char saved_name[sizeof(old_dev->name)];

    if (old_dev == 0 || fresh_dev == 0 ||
        old_dev == fresh_dev ||
        old_dev->blockdev.driver_data != old_dev ||
        fresh_dev->blockdev.driver_data != fresh_dev ||
        fresh_dev->sector_count == 0u) {
        return 0;
    }
    saved_blockdev = old_dev->blockdev;
    memcpy(saved_name, old_dev->name, sizeof(saved_name));

    if (blockdev_unregister(&fresh_dev->blockdev) != 0) {
        return 0;
    }
    if (g_xhci_msc_count != 0u) {
        g_xhci_msc_count--;
    }
    *old_dev = *fresh_dev;
    memcpy(old_dev->name, saved_name, sizeof(old_dev->name));
    old_dev->blockdev = saved_blockdev;
    old_dev->blockdev.name = old_dev->name;
    old_dev->blockdev.block_size = XHCI_SECTOR_SIZE;
    old_dev->blockdev.block_count = old_dev->sector_count;
    old_dev->blockdev.read = fresh_dev->blockdev.read;
    old_dev->blockdev.write = fresh_dev->blockdev.write;
    old_dev->blockdev.flush = fresh_dev->blockdev.flush;
    old_dev->blockdev.reset = fresh_dev->blockdev.reset;
    old_dev->blockdev.driver_data = old_dev;
    old_dev->blockdev.removing = 0u;
    old_dev->blockdev.request_cancelled = 0u;
    old_dev->blockdev.state = BLOCKDEV_STATE_ONLINE;
    old_dev->msc_offline = 0u;
    old_dev->msc_transport_failure_logged = 0u;
    old_dev->msc_transport_error_count = 0u;
    old_dev->msc_recovery_failure_count = 0u;
    old_dev->msc_csw_stall_count = 0u;
    old_dev->msc_csw_stall_reset_due = 0u;
    old_dev->msc_sync_cache_stall_count = 0u;
    old_dev->msc_sync_cache_soft_until = 0u;
    old_dev->msc_single_read_until = 0u;
    fresh_dev->blockdev.driver_data = 0;
    memset(fresh_dev, 0, sizeof(*fresh_dev));
    return 1;
}

int xhci_msc_recover_transport_now(struct xhci_enum_device *dev,
                                    uint8_t op,
                                    const char *phase) {
    return xhci_msc_recover_transport(dev, op, phase, 1u);
}

int xhci_msc_soft_recover_transport_now(struct xhci_enum_device *dev,
                                         uint8_t op,
                                         const char *phase) {
    return xhci_msc_recover_transport(dev, op, phase, 0u);
}

static const char *xhci_msc_phase_name(uint8_t phase) {
    if (phase == 1u) {
        return "CBW";
    }
    if (phase == 2u) {
        return "DATA";
    }
    if (phase == 3u) {
        return "CSW";
    }
    return "idle";
}

static const char *xhci_msc_csw_bad_reason(uint32_t signature,
                                           uint32_t csw_tag,
                                           uint32_t expected_tag,
                                           uint32_t residue,
                                           uint32_t data_len,
                                           uint8_t status) {
    if (signature == MSC_CBW_SIGNATURE) {
        return "cbw-as-csw";
    }
    if (signature != MSC_CSW_SIGNATURE) {
        return "bad-signature";
    }
    if (csw_tag != expected_tag) {
        return "bad-tag";
    }
    if (residue > data_len) {
        return "bad-residue";
    }
    if (status > 2u) {
        return "bad-status";
    }
    return "bad-csw";
}

static void xhci_msc_log_csw_diagnostic(struct xhci_enum_device *dev,
                                        const struct xhci_msc_request_dma *req,
                                        const char *reason,
                                        uint8_t op,
                                        uint32_t expected_tag,
                                        uint32_t signature,
                                        uint32_t csw_tag,
                                        uint8_t status,
                                        uint32_t residue,
                                        uint32_t data_len,
                                        uint32_t data_actual,
                                        uint32_t csw_actual) {
    if (dev == 0) {
        return;
    }
    kprint("xhci: MSC CSW diag reason=%s slot=%u op=%x phase=%u/%s sig=%x tag=%x want=%x status=%u residue=%u data=%u/%u csw=%u cc=%u\n",
           reason != 0 ? reason : "?",
           (uint32_t)dev->slot_id,
           (uint32_t)op,
           (uint32_t)dev->last_msc_phase,
           xhci_msc_phase_name(dev->last_msc_phase),
           signature,
           csw_tag,
           expected_tag,
           (uint32_t)status,
           residue,
           data_actual,
           data_len,
           csw_actual,
           (uint32_t)dev->last_bulk_completion);
    kprint("xhci: MSC CSW bulk epid=%u trb=%lx idx=%u gen=%u req=%u actual=%u residual=%u sense=%x/%x/%x\n",
           (uint32_t)dev->last_bulk_epid,
           dev->last_bulk_trb_phys,
           dev->last_bulk_index,
           dev->last_bulk_generation,
           dev->last_bulk_requested,
           dev->last_bulk_actual,
           dev->last_bulk_residual,
           (uint32_t)dev->last_sense_key,
           (uint32_t)dev->last_sense_asc,
           (uint32_t)dev->last_sense_ascq);
    kprint("xhci: MSC CSW trb buffer=%lx param=%lx status=%x control=%x\n",
           dev->last_bulk_buffer_phys,
           dev->last_bulk_trb_parameter,
           dev->last_bulk_trb_status,
           dev->last_bulk_trb_control);
    if (req != 0) {
        kprint("xhci: MSC CSW dma cbw=%lx data=%lx csw=%lx gen=%u\n",
               req->cbw_phys,
               req->data_phys,
               req->csw_phys,
               req->generation);
        kprint("xhci: MSC CSW raw %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
               (uint32_t)req->csw[0],
               (uint32_t)req->csw[1],
               (uint32_t)req->csw[2],
               (uint32_t)req->csw[3],
               (uint32_t)req->csw[4],
               (uint32_t)req->csw[5],
               (uint32_t)req->csw[6],
               (uint32_t)req->csw[7],
               (uint32_t)req->csw[8],
               (uint32_t)req->csw[9],
               (uint32_t)req->csw[10],
               (uint32_t)req->csw[11],
               (uint32_t)req->csw[12]);
    }
}

int xhci_msc_command(struct xhci_enum_device *dev,
                     const uint8_t *cmd,
                     uint8_t cmd_len,
                     void *buffer,
                     uint32_t data_len,
                     uint8_t data_in) {
    struct xhci_msc_request_dma req;
    uint8_t *payload;
    uint32_t tag;
    uint32_t signature;
    uint32_t csw_tag;
    uint32_t actual_length = 0u;
    uint32_t data_actual = 0u;
    uint32_t data_timeout_ms;
    uint32_t status_timeout_ms;
    int success = 0;
    uint8_t allocated = 0u;
    uint8_t quarantine = 0u;
    uint8_t csw_received = 0u;

    if (dev == 0 || dev->msc_offline ||
        cmd == 0 || cmd_len == 0u || cmd_len > 16u ||
        data_len > XHCI_MSC_DMA_PAYLOAD_MAX || (data_len != 0u && buffer == 0)) {
        return 0;
    }
    memset(&req, 0, sizeof(req));
    if (!xhci_msc_request_dma_alloc(dev, &req)) {
        xhci_msc_mark_offline(dev, "request-dma-alloc");
        return 0;
    }
    allocated = 1u;
    if (!xhci_msc_request_dma_layout_ok(&req, data_len, cmd[0])) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_mark_offline(dev, "request-dma-overlap");
        quarantine = 1u;
        goto out;
    }
    payload = xhci_msc_data_payload(&req);
    data_timeout_ms = xhci_msc_data_timeout_ms(cmd[0], data_in);
    status_timeout_ms = xhci_msc_status_timeout_ms(cmd[0]);
    blockdev_extend_request_deadline_ms(&dev->blockdev,
                                        xhci_msc_request_deadline_ms(cmd[0]));
    dev->last_msc_phase = 0u;
    dev->last_msc_status = 0u;
    dev->last_msc_csw_stalled = 0u;
    dev->last_msc_residue = 0u;
    memset(req.cbw, 0, 31u);
    memset(req.csw, 0, 13u);
    tag = ++dev->tag;
    usb_write_u32le(req.cbw + 0, MSC_CBW_SIGNATURE);
    usb_write_u32le(req.cbw + 4, tag);
    usb_write_u32le(req.cbw + 8, data_len);
    req.cbw[12] = data_in ? 0x80u : 0u;
    req.cbw[13] = dev->msc_lun;
    req.cbw[14] = cmd_len;
    memcpy(req.cbw + 15, cmd, cmd_len);
    if (buffer != 0 && data_len != 0u && !data_in) {
        memcpy(payload, buffer, data_len);
    }
    dev->last_msc_phase = 1u;
    if (!xhci_bulk_transfer(dev,
                            dev->bulk_out_epid,
                            req.cbw_phys,
                            31u,
                            &actual_length,
                            3000u) ||
        actual_length != 31u) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC CBW failed slot=%u tag=%x op=%x\n",
                                 (uint32_t)dev->slot_id,
                                 tag,
                                 (uint32_t)cmd[0]);
        quarantine = 1u;
        goto out;
    }
    if (data_len != 0u) {
        if (data_in) {
            memset(payload, 0, data_len);
        }
        dev->last_msc_phase = 2u;
        if (!xhci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_epid : dev->bulk_out_epid,
                                xhci_msc_data_payload_phys(&req),
                                data_len,
                                &data_actual,
                                data_timeout_ms)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            XHCI_MSC_TRANSPORT_TRACE("xhci: MSC DATA %s failed slot=%u tag=%x op=%x len=%u\n",
                                     data_in ? "in" : "out",
                                     (uint32_t)dev->slot_id,
                                     tag,
                                     (uint32_t)cmd[0],
                                     data_len);
            quarantine = 1u;
            goto out;
        }
        if (!xhci_msc_dma_guard_ok(req.data)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("xhci: MSC DMA guard damaged slot=%u tag=%x op=%x len=%u\n",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   data_len);
            xhci_msc_mark_offline(dev, "dma-guard");
            quarantine = 1u;
            goto out;
        }
    }
    dev->last_msc_phase = 3u;
    for (uint32_t csw_attempt = 0u;
         csw_attempt <= XHCI_MSC_CSW_STALL_RETRY_LIMIT;
         csw_attempt++) {
        memset(req.csw, 0xcc, 13u);
        actual_length = 0u;
        if (xhci_bulk_transfer(dev,
                               dev->bulk_in_epid,
                               req.csw_phys,
                               13u,
                               &actual_length,
                               status_timeout_ms) &&
            actual_length == 13u) {
            csw_received = 1u;
            break;
        }
        if (dev->last_bulk_completion != XHCI_CC_STALL_ERROR || csw_attempt != 0u) {
            break;
        }
        xhci_msc_note_csw_stall(dev, cmd[0]);
        xhci_msc_log_csw_stall_retry(dev, tag, cmd[0], dev->last_bulk_residual);
        if (!xhci_clear_endpoint_halt(dev, dev->bulk_in_ep) ||
            !xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid)) {
            break;
        }
        xhci_drain_transfer_events(dev->slot_id, dev->bulk_in_epid, 8u);
        xhci_delay_ms(20u);
    }
    if (!csw_received) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_check_csw_submission(dev, &req, cmd[0], 0xffffffffu);
        if (xhci_msc_csw_pattern_unchanged(&req)) {
            kprint("xhci: MSC CSW pattern unchanged slot=%u tag=%x op=%x csw=%lx actual=%u residual=%u cc=%u\n",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   req.csw_phys,
                   actual_length,
                   dev->last_bulk_residual,
                   (uint32_t)dev->last_bulk_completion);
        }
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC CSW failed slot=%u tag=%x op=%x cc=%u\n",
                                 (uint32_t)dev->slot_id,
                                 tag,
                                 (uint32_t)cmd[0],
                                 (uint32_t)dev->last_bulk_completion);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    "csw-transfer",
                                    cmd[0],
                                    tag,
                                    usb_read_u32le(req.csw),
                                    usb_read_u32le(req.csw + 4),
                                    req.csw[12],
                                    usb_read_u32le(req.csw + 8),
                                    data_len,
                                    data_actual,
                                    actual_length);
        quarantine = 1u;
        goto out;
    }
    xhci_msc_check_csw_submission(dev, &req, cmd[0], 13u);
    if (xhci_msc_csw_pattern_unchanged(&req)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("xhci: MSC CSW pattern unchanged slot=%u tag=%x op=%x csw=%lx actual=%u residual=%u cc=%u\n",
               (uint32_t)dev->slot_id,
               tag,
               (uint32_t)cmd[0],
               req.csw_phys,
               actual_length,
               dev->last_bulk_residual,
               (uint32_t)dev->last_bulk_completion);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    "csw-dma-not-written",
                                    cmd[0],
                                    tag,
                                    usb_read_u32le(req.csw),
                                    usb_read_u32le(req.csw + 4),
                                    req.csw[12],
                                    usb_read_u32le(req.csw + 8),
                                    data_len,
                                    data_actual,
                                    actual_length);
        xhci_msc_mark_offline(dev, "csw-dma-not-written");
        quarantine = 1u;
        goto out;
    }
    signature = usb_read_u32le(req.csw);
    csw_tag = usb_read_u32le(req.csw + 4);
    dev->last_msc_residue = usb_read_u32le(req.csw + 8);
    dev->last_msc_status = req.csw[12];
    if (signature != MSC_CSW_SIGNATURE ||
        csw_tag != tag ||
        dev->last_msc_residue > data_len ||
        dev->last_msc_status > 2u) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC CSW bad slot=%u op=%x sig=%x tag=%x/%x status=%u residue=%x\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)cmd[0],
                                 signature,
                                 csw_tag,
                                 tag,
                                 (uint32_t)dev->last_msc_status,
                                 dev->last_msc_residue);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    xhci_msc_csw_bad_reason(signature,
                                                            csw_tag,
                                                            tag,
                                                            dev->last_msc_residue,
                                                            data_len,
                                                            dev->last_msc_status),
                                    cmd[0],
                                    tag,
                                    signature,
                                    csw_tag,
                                    dev->last_msc_status,
                                    dev->last_msc_residue,
                                    data_len,
                                    data_actual,
                                    actual_length);
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_mark_offline(dev, "bad-csw");
        quarantine = 1u;
        goto out;
    }
    if (data_len != 0u &&
        (uint64_t)data_actual + (uint64_t)dev->last_msc_residue != data_len) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC DATA/CSW length mismatch slot=%u op=%x actual=%u residue=%u expected=%u\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)cmd[0],
                                 data_actual,
                                 dev->last_msc_residue,
                                 data_len);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    "data-csw-length",
                                    cmd[0],
                                    tag,
                                    signature,
                                    csw_tag,
                                    dev->last_msc_status,
                                    dev->last_msc_residue,
                                    data_len,
                                    data_actual,
                                    actual_length);
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_mark_offline(dev, "data-csw-length-mismatch");
        quarantine = 1u;
        goto out;
    }
    if (dev->last_msc_status == 2u) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC CSW phase error slot=%u op=%x residue=%x\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)cmd[0],
                                 dev->last_msc_residue);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    "csw-phase",
                                    cmd[0],
                                    tag,
                                    signature,
                                    csw_tag,
                                    dev->last_msc_status,
                                    dev->last_msc_residue,
                                    data_len,
                                    data_actual,
                                    actual_length);
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_mark_offline(dev, "csw-phase");
        quarantine = 1u;
        goto out;
    }
    if (dev->last_msc_status == 0u &&
        dev->last_msc_residue != 0u &&
        xhci_msc_is_block_rw(cmd[0])) {
        XHCI_MSC_TRANSPORT_TRACE("xhci: MSC short block transfer slot=%u op=%x residue=%x\n",
                                 (uint32_t)dev->slot_id,
                                 (uint32_t)cmd[0],
                                 dev->last_msc_residue);
        xhci_msc_log_csw_diagnostic(dev,
                                    &req,
                                    "short-block",
                                    cmd[0],
                                    tag,
                                    signature,
                                    csw_tag,
                                    dev->last_msc_status,
                                    dev->last_msc_residue,
                                    data_len,
                                    data_actual,
                                    actual_length);
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        xhci_msc_mark_offline(dev, "short-block-transfer");
        quarantine = 1u;
        goto out;
    }
    if (dev->last_msc_status != 0u) {
        if (cmd[0] != SCSI_TEST_UNIT_READY &&
            cmd[0] != SCSI_READ_CAPACITY_10 &&
            cmd[0] != SCSI_READ_CAPACITY_16 &&
            cmd[0] != SCSI_REQUEST_SENSE) {
            XHCI_MSC_TRANSPORT_TRACE("xhci: MSC command status slot=%u op=%x status=%u residue=%x\n",
                                     (uint32_t)dev->slot_id,
                                     (uint32_t)cmd[0],
                                     (uint32_t)dev->last_msc_status,
                                     dev->last_msc_residue);
        }
        dev->last_msc_phase = 0u;
        goto out;
    }
    if (data_len != 0u && !xhci_msc_dma_guard_ok(req.data)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("xhci: MSC DMA guard damaged after CSW slot=%u tag=%x op=%x len=%u\n",
               (uint32_t)dev->slot_id,
               tag,
               (uint32_t)cmd[0],
               data_len);
        xhci_msc_mark_offline(dev, "dma-guard-csw");
        quarantine = 1u;
        goto out;
    }
    if (buffer != 0 && data_in && data_len != 0u) {
        memcpy(buffer, payload, data_len);
    }
    xhci_msc_note_clean_csw(dev, cmd[0]);
    if (dev->msc_csw_stall_reset_due != 0u) {
        dev->msc_csw_stall_reset_due = 0u;
        dev->msc_csw_stall_count = 0u;
        kprint("xhci: MSC CSW stall budget reached, BOT reset slot=%u op=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)cmd[0]);
        (void)xhci_msc_standard_recover_transport(dev,
                                                  cmd[0],
                                                  "csw-stall-budget",
                                                  3u);
    }
    dev->last_msc_phase = 0u;
    dev->msc_transport_failure_logged = 0u;
    success = 1;

out:
    if (allocated) {
        if (quarantine) {
            if (!xhci_msc_request_dma_quarantine(&req)) {
                xhci_msc_mark_offline(dev, "dma-quarantine-full");
            }
        } else {
            xhci_msc_request_dma_free(&req);
        }
    }
    return success;
}

int xhci_msc_command_recover(struct xhci_enum_device *dev,
                             const uint8_t *cmd,
                             uint8_t cmd_len,
                             void *buffer,
                             uint32_t data_len,
                             uint8_t data_in) {
    uint8_t failed_phase;

    if (xhci_msc_command(dev, cmd, cmd_len, buffer, data_len, data_in)) {
        return 1;
    }
    failed_phase = dev != 0 ? dev->last_msc_phase : 0u;
    if (dev != 0 && !dev->msc_offline &&
        (failed_phase == 1u ||
         failed_phase == 2u ||
         failed_phase == 3u)) {
        if (!xhci_msc_recover_transport(dev, cmd != 0 ? cmd[0] : 0u, "recover", 1u)) {
            return 0;
        }
        return xhci_msc_command(dev, cmd, cmd_len, buffer, data_len, data_in);
    }
    return 0;
}

static int xhci_msc_read_capacity(struct xhci_enum_device *dev) {
    uint8_t cmd[16];
    uint8_t cap[32];
    uint64_t last_lba;
    uint32_t block_len;

    memset(cmd, 0, sizeof(cmd));
    memset(cap, 0, sizeof(cap));
    cmd[0] = SCSI_READ_CAPACITY_10;
    if (!xhci_msc_command_recover(dev, cmd, 10u, cap, 8u, 1u)) {
        return 0;
    }
    last_lba = usb_read_u32be(cap);
    block_len = usb_read_u32be(cap + 4);
    if (block_len != XHCI_SECTOR_SIZE) {
        kprint("xhci: MSC unsupported block size=%x last_lba=%lx\n",
               block_len,
               last_lba);
        return 0;
    }
    if (last_lba == 0xffffffffull) {
        memset(cmd, 0, sizeof(cmd));
        memset(cap, 0, sizeof(cap));
        cmd[0] = SCSI_READ_CAPACITY_16;
        cmd[1] = 0x10u;
        usb_write_u32be(cmd + 10, sizeof(cap));
        if (!xhci_msc_command_recover(dev, cmd, 16u, cap, sizeof(cap), 1u)) {
            kprint("xhci: MSC READ CAPACITY(16) failed\n");
            return 0;
        }
        last_lba = usb_read_u64be(cap);
        block_len = usb_read_u32be(cap + 8);
        if (block_len != XHCI_SECTOR_SIZE) {
            kprint("xhci: MSC unsupported block size=%x last_lba=%lx\n",
                   block_len,
                   last_lba);
            return 0;
        }
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return 1;
}

static int xhci_msc_test_unit_ready(struct xhci_enum_device *dev) {
    uint8_t cmd[6];

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return xhci_msc_command_recover(dev, cmd, 6u, 0, 0u, 0u);
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

int xhci_msc_medium_not_present(const struct xhci_enum_device *dev) {
    return dev != 0 && dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au;
}

void xhci_msc_retry_delay(uint8_t failed_phase, uint8_t failed_status) {
    if (failed_status == MSC_STATUS_TRANSPORT_ERROR ||
        failed_phase == 1u ||
        failed_phase == 2u ||
        failed_phase == 3u) {
        xhci_delay_ms(25u);
        return;
    }
    xhci_delay_ms(10u);
}

int xhci_msc_buffer_has_transport_signature(const uint8_t *data) {
    uint32_t signature;

    if (data == 0) {
        return 0;
    }
    signature = usb_read_u32le(data);
    return signature == MSC_CBW_SIGNATURE || signature == MSC_CSW_SIGNATURE;
}

int xhci_msc_request_sense(struct xhci_enum_device *dev) {
    uint8_t cmd[6];
    uint8_t sense[18];
    int ok;

    if (dev == 0) {
        return 0;
    }
    memset(cmd, 0, sizeof(cmd));
    memset(sense, 0, sizeof(sense));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = sizeof(sense);
    ok = xhci_msc_command_recover(dev, cmd, 6u, sense, sizeof(sense), 1u);
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

    dev->msc_transport_failure_logged = 0u;
    dev->msc_transport_error_count = 0u;
    dev->msc_offline = 0u;
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
            if (xhci_msc_command_recover(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
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
        XHCI_MSC_CONFIG_TRACE("xhci: MSC lun%u inquiry type=%x removable=%u\n",
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


void xhci_probe_msc(struct xhci_enum_device *dev) {
    if (g_xhci_msc_count >= XHCI_MAX_ENUM_DEVICES) {
        return;
    }
    if (!xhci_alloc_msc_resources(dev)) {
        kprint("xhci: slot%u MSC resource allocation failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_control_set_configuration(dev, dev->configuration)) {
        kprint("xhci: slot%u MSC set config failed\n", (uint32_t)dev->slot_id);
        xhci_release_msc_resources(dev);
        return;
    }
    xhci_delay_ms(XHCI_MSC_CONFIG_SETTLE_MS);
    if (!xhci_configure_bulk_endpoints(dev)) {
        kprint("xhci: slot%u MSC endpoint config failed in=%u out=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_epid,
               (uint32_t)dev->bulk_out_epid);
        xhci_release_msc_resources(dev);
        return;
    }
    xhci_delay_ms(50u);
    dev->tag = 0x56780000u;
    if (!xhci_msc_probe_device(dev)) {
        kprint("xhci: slot%u MSC probe failed\n", (uint32_t)dev->slot_id);
        xhci_release_msc_resources(dev);
        return;
    }
    if (!xhci_msc_register_blockdev(dev)) {
        xhci_release_msc_resources(dev);
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
