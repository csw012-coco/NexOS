#include "kernel/internal/mem/vmm_transfer.h"

#include "lib/string.h"

static uint64_t vmm_transfer_align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1u);
}

static uint64_t vmm_transfer_align_up(uint64_t value, uint64_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

int vmm_transfer_user_range_bounds(const struct vmm_transfer_ops *ops,
                                   uint64_t user_addr,
                                   uint64_t size,
                                   uint64_t *start_out,
                                   uint64_t *end_out) {
    uint64_t end;

    if (ops == 0 ||
        ops->page_size == 0u ||
        user_addr < ops->user_base ||
        user_addr >= ops->user_limit) {
        return 0;
    }
    if (size == 0u) {
        if (start_out != 0) {
            *start_out = user_addr;
        }
        if (end_out != 0) {
            *end_out = user_addr;
        }
        return 1;
    }
    end = user_addr + size;
    if (end < user_addr || end > ops->user_limit) {
        return 0;
    }
    if (start_out != 0) {
        *start_out = vmm_transfer_align_down(user_addr, ops->page_size);
    }
    if (end_out != 0) {
        *end_out = vmm_transfer_align_up(end, ops->page_size);
    }
    return 1;
}

int vmm_transfer_user_access_range_bounds(const struct vmm_page_access_ops *ops,
                                          uint64_t user_addr,
                                          uint64_t size,
                                          uint64_t *start_out,
                                          uint64_t *end_out) {
    uint64_t end;

    if (ops == 0 ||
        ops->page_size == 0u ||
        user_addr < ops->user_base ||
        user_addr >= ops->user_limit) {
        return 0;
    }
    if (size == 0u) {
        if (start_out != 0) {
            *start_out = user_addr;
        }
        if (end_out != 0) {
            *end_out = user_addr;
        }
        return 1;
    }
    end = user_addr + size;
    if (end < user_addr || end > ops->user_limit) {
        return 0;
    }
    if (start_out != 0) {
        *start_out = vmm_transfer_align_down(user_addr, ops->page_size);
    }
    if (end_out != 0) {
        *end_out = vmm_transfer_align_up(end, ops->page_size);
    }
    return 1;
}

int vmm_transfer_user_range_accessible(const struct vmm_page_access_ops *ops,
                                       uint64_t user_addr,
                                       uint64_t size,
                                       int writable,
                                       void *context) {
    uint64_t page;
    uint64_t end;

    if (ops == 0 || ops->page_accessible == 0) {
        return 0;
    }
    if (!vmm_transfer_user_access_range_bounds(ops, user_addr, size, &page, &end)) {
        return 0;
    }
    while (page < end) {
        if (!ops->page_accessible(page, writable, context)) {
            return 0;
        }
        page += ops->page_size;
    }
    return 1;
}

uint32_t vmm_transfer_page_chunk_size(const struct vmm_transfer_ops *ops,
                                      uint64_t virt,
                                      uint64_t remaining,
                                      uint64_t *page_off_out) {
    uint64_t page_off;
    uint64_t chunk;

    if (ops == 0 || ops->page_size == 0u) {
        return 0u;
    }
    page_off = virt & (ops->page_size - 1u);
    chunk = ops->page_size - page_off;
    if (chunk > remaining) {
        chunk = remaining;
    }
    if (page_off_out != 0) {
        *page_off_out = page_off;
    }
    return (uint32_t)chunk;
}

int vmm_transfer_copy_from_user(const struct vmm_transfer_ops *ops,
                                void *dest,
                                uint64_t user_addr,
                                uint32_t size) {
    uint8_t *out = (uint8_t *)dest;
    uint32_t offset = 0u;

    if (ops == 0 ||
        ops->readable == 0 ||
        ops->read_ptr == 0 ||
        dest == 0 ||
        !vmm_transfer_user_range_bounds(ops, user_addr, size, 0, 0)) {
        return 0;
    }
    while (offset < size) {
        uint64_t virt = user_addr + offset;
        uint32_t chunk = vmm_transfer_page_chunk_size(ops,
                                                      virt,
                                                      size - offset,
                                                      0);
        const void *src;

        if (chunk == 0u || !ops->readable(virt, chunk)) {
            return 0;
        }
        src = ops->read_ptr(virt);
        if (src == 0) {
            return 0;
        }
        memcpy(out + offset, src, chunk);
        offset += chunk;
    }
    return 1;
}

int vmm_transfer_copy_to_user(const struct vmm_transfer_ops *ops,
                              uint64_t user_addr,
                              const void *src,
                              uint32_t size) {
    const uint8_t *in = (const uint8_t *)src;
    uint32_t offset = 0u;

    if (ops == 0 ||
        ops->writable == 0 ||
        ops->write_ptr == 0 ||
        src == 0 ||
        !vmm_transfer_user_range_bounds(ops, user_addr, size, 0, 0)) {
        return 0;
    }
    while (offset < size) {
        uint64_t virt = user_addr + offset;
        uint32_t chunk = vmm_transfer_page_chunk_size(ops,
                                                      virt,
                                                      size - offset,
                                                      0);
        void *dest;

        if (chunk == 0u || !ops->writable(virt, chunk)) {
            return 0;
        }
        dest = ops->write_ptr(virt);
        if (dest == 0) {
            return 0;
        }
        memcpy(dest, in + offset, chunk);
        offset += chunk;
    }
    return 1;
}

int vmm_transfer_copy_user_cstr(const struct vmm_transfer_ops *ops,
                                char *dest,
                                uint64_t user_addr,
                                uint32_t max_len) {
    uint32_t copied = 0u;

    if (ops == 0 ||
        ops->readable == 0 ||
        ops->read_ptr == 0 ||
        dest == 0 ||
        max_len == 0u) {
        return 0;
    }
    while (copied + 1u < max_len) {
        uint64_t virt = user_addr + copied;
        uint32_t chunk = vmm_transfer_page_chunk_size(
            ops,
            virt,
            max_len - 1u - copied,
            0);
        const char *src;

        if (virt < user_addr || chunk == 0u || !ops->readable(virt, chunk)) {
            return 0;
        }
        src = (const char *)ops->read_ptr(virt);
        if (src == 0) {
            return 0;
        }
        for (uint32_t i = 0u; i < chunk; i++) {
            char ch = src[i];

            dest[copied++] = ch;
            if (ch == '\0') {
                return 1;
            }
        }
    }
    dest[max_len - 1u] = '\0';
    return 1;
}

int vmm_transfer_zero_range(const struct vmm_transfer_ops *ops,
                            uint64_t start,
                            uint64_t size) {
    uint64_t offset = 0u;

    if (ops == 0 ||
        ops->writable == 0 ||
        ops->write_ptr == 0 ||
        !vmm_transfer_user_range_bounds(ops, start, size, 0, 0)) {
        return 0;
    }
    while (offset < size) {
        uint64_t virt = start + offset;
        uint32_t chunk = vmm_transfer_page_chunk_size(ops,
                                                      virt,
                                                      size - offset,
                                                      0);
        void *dest;

        if (chunk == 0u || !ops->writable(virt, chunk)) {
            return 0;
        }
        dest = ops->write_ptr(virt);
        if (dest == 0) {
            return 0;
        }
        memset(dest, 0, chunk);
        offset += chunk;
    }
    return 1;
}
