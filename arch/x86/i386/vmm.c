#include "arch/x86/i386/paging.h"
#include "kernel/internal/mem/vmm_transfer.h"
#include "kernel/public/mem/vmm.h"
#include "lib/string.h"

enum {
    VMM_I386_FLAG_PRESENT = 1u << 0,
    VMM_I386_FLAG_RW = 1u << 1,
    VMM_I386_FLAG_USER = 1u << 2
};

int vmm_user_readable(uint64_t user_addr, uint32_t size);
int vmm_user_writable(uint64_t user_addr, uint32_t size);
static int vmm_i386_page_accessible(uint64_t page,
                                    int writable,
                                    void *context);

static int vmm_i386_addr32(uint64_t value, uint32_t *out) {
    if (out == 0 || value > 0xffffffffull) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int vmm_i386_size32(uint64_t value, uint32_t *out) {
    if (out == 0 || value > 0xffffffffull) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int vmm_i386_with_kernel_root(uint32_t *old_root_out) {
    uint32_t old_root;
    uint32_t kernel_root;

    if (old_root_out == 0) {
        return 0;
    }
    old_root = i386_paging_root();
    kernel_root = i386_paging_kernel_root();
    if (old_root == 0u || kernel_root == 0u) {
        return 0;
    }
    if (old_root != kernel_root) {
        i386_paging_switch(kernel_root);
    }
    *old_root_out = old_root;
    return 1;
}

static void vmm_i386_restore_root(uint32_t old_root) {
    if (old_root != 0u && i386_paging_root() != old_root) {
        i386_paging_switch(old_root);
    }
}

static int vmm_i386_page_accessible(uint64_t page,
                                    int writable,
                                    void *context) {
    uint32_t root;
    uint32_t page32;

    if (context == 0 ||
        !vmm_i386_addr32(page, &page32)) {
        return 0;
    }
    root = *(const uint32_t *)context;
    return i386_paging_user_accessible_in(root, page32, writable);
}

static const struct vmm_page_access_ops vmm_i386_page_access_ops = {
    I386_PAGE_SIZE,
    I386_PAGING_IDENTITY_LIMIT,
    0xc0000000ull,
    vmm_i386_page_accessible
};

static int vmm_i386_user_range_valid(uint64_t user_addr,
                                     uint32_t size,
                                     int writable) {
    uint32_t old_root;
    int valid;

    if (!vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    valid = vmm_transfer_user_range_accessible(&vmm_i386_page_access_ops,
                                               user_addr,
                                               size,
                                               writable,
                                               &old_root);
    vmm_i386_restore_root(old_root);
    return valid;
}

static const void *vmm_i386_read_ptr(uint64_t user_addr) {
    uint32_t addr;

    return vmm_i386_addr32(user_addr, &addr)
        ? (const void *)(uintptr_t)addr
        : 0;
}

static void *vmm_i386_write_ptr(uint64_t user_addr) {
    uint32_t addr;

    return vmm_i386_addr32(user_addr, &addr)
        ? (void *)(uintptr_t)addr
        : 0;
}

static const struct vmm_transfer_ops vmm_i386_transfer_ops = {
    I386_PAGE_SIZE,
    I386_PAGING_IDENTITY_LIMIT,
    0xc0000000ull,
    vmm_user_readable,
    vmm_user_writable,
    vmm_i386_read_ptr,
    vmm_i386_write_ptr
};

uint64_t vmm_current_root(void) {
    return i386_paging_root();
}

uint64_t vmm_create_user_root(void) {
    uint32_t old_root;
    uint32_t root;

    if (!vmm_i386_with_kernel_root(&old_root)) {
        return 0u;
    }
    root = i386_paging_create_address_space();
    vmm_i386_restore_root(old_root);
    return root;
}

uint64_t vmm_clone_root_cow(uint64_t source_root) {
    uint32_t source;
    uint32_t old_root;
    uint32_t clone;

    if (!vmm_i386_addr32(source_root, &source) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0u;
    }
    clone = i386_paging_clone_user_cow(source);
    vmm_i386_restore_root(old_root);
    return clone;
}

int vmm_resolve_cow_fault(uint64_t root, uint64_t fault_addr, uint64_t error_code) {
    uint32_t root32;
    uint32_t fault32;
    uint32_t old_root;
    int ok;

    if (!vmm_i386_addr32(root, &root32) ||
        !vmm_i386_addr32(fault_addr, &fault32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    ok = i386_paging_resolve_cow_fault(root32, fault32, (uint32_t)error_code);
    vmm_i386_restore_root(old_root);
    return ok;
}

void vmm_destroy_user_root(uint64_t root) {
    uint32_t root32;
    uint32_t old_root;

    if (!vmm_i386_addr32(root, &root32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return;
    }
    i386_paging_destroy_user_space(root32);
    vmm_i386_restore_root(old_root);
}

void vmm_switch_root(uint64_t root) {
    uint32_t root32;

    if (vmm_i386_addr32(root, &root32)) {
        i386_paging_switch(root32);
    }
}

int vmm_root_is_current(uint64_t root) {
    return root != 0u && root <= 0xffffffffull && i386_paging_root() == (uint32_t)root;
}

int vmm_switch_root_or_fail(uint64_t root) {
    vmm_switch_root(root);
    return vmm_root_is_current(root);
}

void vmm_allow_user_page(uint64_t addr) {
    (void)addr;
}

void vmm_allow_user_range(uint64_t start, uint64_t end) {
    (void)start;
    (void)end;
}

void vmm_set_supervisor_range(uint64_t start, uint64_t end) {
    (void)start;
    (void)end;
}

int vmm_map(uint64_t virt_addr, uint64_t phys_addr, uint32_t perms) {
    uint32_t virt32;
    uint32_t phys32;
    uint32_t root;
    uint32_t old_root;
    int ok;

    if (!vmm_i386_addr32(virt_addr, &virt32) ||
        !vmm_i386_addr32(phys_addr, &phys32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    root = old_root;
    if (root == i386_paging_kernel_root()) {
        ok = i386_paging_map_page(virt32,
                                  phys32,
                                  (perms & VMM_PERM_WRITE) != 0,
                                  (perms & VMM_PERM_USER) != 0);
    } else {
        ok = i386_paging_map_page_in(root,
                                     virt32,
                                     phys32,
                                     (perms & VMM_PERM_WRITE) != 0,
                                     (perms & VMM_PERM_USER) != 0);
    }
    vmm_i386_restore_root(old_root);
    return ok;
}

int vmm_unmap(uint64_t virt_addr, uint64_t *phys_addr) {
    uint32_t virt32;
    uint32_t phys32 = 0u;
    uint32_t root;
    uint32_t old_root;
    int ok;

    if (!vmm_i386_addr32(virt_addr, &virt32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    root = old_root;
    ok = root == i386_paging_kernel_root()
        ? i386_paging_unmap_page(virt32, &phys32)
        : i386_paging_unmap_page_in(root, virt32, &phys32);
    vmm_i386_restore_root(old_root);
    if (ok && phys_addr != 0) {
        *phys_addr = phys32;
    }
    return ok;
}

int vmm_query(uint64_t virt_addr, uint64_t *phys_addr) {
    uint32_t virt32;
    uint32_t phys32;
    uint32_t old_root;
    int ok;

    if (phys_addr == 0 ||
        !vmm_i386_addr32(virt_addr, &virt32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    ok = i386_paging_translate_in(old_root, virt32, &phys32);
    vmm_i386_restore_root(old_root);
    if (ok) {
        *phys_addr = phys32;
    }
    return ok;
}

int vmm_query_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags) {
    uint64_t phys;
    uint64_t out_flags = VMM_I386_FLAG_PRESENT;

    if (!vmm_query(virt_addr, &phys)) {
        return 0;
    }
    if (vmm_i386_user_range_valid(virt_addr, 1u, 0)) {
        out_flags |= VMM_I386_FLAG_USER;
        if (vmm_i386_user_range_valid(virt_addr, 1u, 1)) {
            out_flags |= VMM_I386_FLAG_RW;
        }
    }
    if (phys_addr != 0) {
        *phys_addr = phys;
    }
    if (flags != 0) {
        *flags = out_flags;
    }
    return 1;
}

int vmm_cpu_supports_nx(void) {
    return 0;
}

int vmm_nx_enabled(void) {
    return 0;
}

int vmm_user_readable(uint64_t user_addr, uint32_t size) {
    if (size == 0u) {
        return vmm_transfer_user_range_bounds(&vmm_i386_transfer_ops,
                                              user_addr,
                                              0u,
                                              0,
                                              0);
    }
    return vmm_i386_user_range_valid(user_addr, size, 0);
}

int vmm_user_writable(uint64_t user_addr, uint32_t size) {
    if (size == 0u) {
        return vmm_transfer_user_range_bounds(&vmm_i386_transfer_ops,
                                              user_addr,
                                              0u,
                                              0,
                                              0);
    }
    return vmm_i386_user_range_valid(user_addr, size, 1);
}

int vmm_user_page_mapped(uint64_t user_addr) {
    uint64_t phys;
    uint64_t flags;

    if ((user_addr & (uint64_t)(I386_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }
    if (!vmm_query_info(user_addr, &phys, &flags)) {
        return 0;
    }
    return (flags & (VMM_I386_FLAG_PRESENT | VMM_I386_FLAG_USER)) ==
           (VMM_I386_FLAG_PRESENT | VMM_I386_FLAG_USER);
}

int vmm_copy_from_user(void *dest, uint64_t user_addr, uint32_t size) {
    return vmm_transfer_copy_from_user(&vmm_i386_transfer_ops,
                                       dest,
                                       user_addr,
                                       size);
}

int vmm_copy_to_user(uint64_t user_addr, const void *src, uint32_t size) {
    return vmm_transfer_copy_to_user(&vmm_i386_transfer_ops,
                                     user_addr,
                                     src,
                                     size);
}

int vmm_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len) {
    return vmm_transfer_copy_user_cstr(&vmm_i386_transfer_ops,
                                       dest,
                                       user_addr,
                                       max_len);
}

int vmm_zero_range(uint64_t start, uint64_t size) {
    return vmm_transfer_zero_range(&vmm_i386_transfer_ops, start, size);
}

int vmm_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size) {
    uint32_t size32;

    if (!vmm_i386_size32(size, &size32)) {
        return 0;
    }
    return vmm_copy_to_user(dest, src, size32);
}

void vmm_unmap_range_if_present(uint64_t start, uint64_t end) {
    uint64_t page;
    uint64_t page_end;

    if (end <= start ||
        !vmm_transfer_user_range_bounds(&vmm_i386_transfer_ops,
                                        start,
                                        end - start,
                                        &page,
                                        &page_end)) {
        return;
    }
    while (page < page_end) {
        uint64_t phys;

        if (vmm_query(page, &phys)) {
            (void)vmm_unmap(page, 0);
        }
        page += I386_PAGE_SIZE;
    }
}

uint64_t vmm_get_current_cr3(void) {
    return vmm_current_root();
}

int vmm_query_mapping_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                 uint64_t *phys_addr, uint64_t *flags) {
    uint32_t root;
    uint32_t virt32;
    uint32_t phys32;
    uint32_t old_root;
    int ok;

    if (!vmm_i386_addr32(context_cr3, &root) ||
        !vmm_i386_addr32(virt_addr, &virt32) ||
        !vmm_i386_with_kernel_root(&old_root)) {
        return 0;
    }
    ok = i386_paging_translate_in(root, virt32, &phys32);
    vmm_i386_restore_root(old_root);
    if (ok) {
        if (phys_addr != 0) {
            *phys_addr = phys32;
        }
        if (flags != 0) {
            *flags = VMM_I386_FLAG_PRESENT;
        }
    }
    return ok;
}

int vmm_query_page_walk_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                   uint64_t *pml4e, uint64_t *pdpte,
                                   uint64_t *pde, uint64_t *pte) {
    (void)context_cr3;
    (void)virt_addr;
    if (pml4e != 0) *pml4e = 0u;
    if (pdpte != 0) *pdpte = 0u;
    if (pde != 0) *pde = 0u;
    if (pte != 0) *pte = 0u;
    return 0;
}

int vmm_query_page_walk(uint64_t virt_addr,
                        uint64_t *pml4e, uint64_t *pdpte,
                        uint64_t *pde, uint64_t *pte) {
    return vmm_query_page_walk_in_context(vmm_current_root(),
                                          virt_addr,
                                          pml4e,
                                          pdpte,
                                          pde,
                                          pte);
}

int vmm_query_page_walk_full(uint64_t context_cr3, uint64_t virt_addr,
                             struct vmm_page_walk_info *info_out) {
    (void)context_cr3;
    (void)virt_addr;
    if (info_out != 0) {
        memset(info_out, 0, sizeof(*info_out));
    }
    return 0;
}

void vmm_get_page_fault_trace(struct vmm_page_fault_trace *trace_out) {
    if (trace_out != 0) {
        memset(trace_out, 0, sizeof(*trace_out));
    }
}

void vmm_get_page_clone_trace(struct vmm_page_clone_trace *trace_out) {
    if (trace_out != 0) {
        memset(trace_out, 0, sizeof(*trace_out));
    }
}
