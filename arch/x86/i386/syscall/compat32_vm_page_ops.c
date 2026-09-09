#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

enum {
    I386_VM_PAGE_SIZE = 4096u,
    I386_MMAP_MAX_PAGES = 16u
};

static uint64_t syscall_compat32_vm_page_alloc(void *ctx_ptr) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || ctx->page_alloc == 0) {
        return 0u;
    }
    return ctx->page_alloc();
}

static uint64_t syscall_compat32_vm_page_alloc_prot(void *ctx_ptr,
                                                uint32_t writable) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || ctx->page_alloc_prot == 0) {
        return 0u;
    }
    return ctx->page_alloc_prot(writable != 0u);
}

static uint64_t syscall_compat32_vm_page_alloc_at(void *ctx_ptr,
                                              uint64_t user_page,
                                              uint32_t writable) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || ctx->page_alloc_at == 0 ||
        user_page > 0xffffffffu) {
        return 0u;
    }
    return ctx->page_alloc_at((uint32_t)user_page, writable != 0u);
}

static uint64_t syscall_compat32_vm_page_free(void *ctx_ptr,
                                          uint64_t user_page) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || ctx->page_free == 0 || user_page > 0xffffffffu) {
        return (uint64_t)(uint32_t)-1;
    }
    return (uint64_t)(uint32_t)ctx->page_free((uint32_t)user_page);
}

static uint64_t syscall_compat32_vm_page_protect(void *ctx_ptr,
                                             uint64_t user_page,
                                             uint32_t writable) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || ctx->page_protect == 0 || user_page > 0xffffffffu) {
        return (uint64_t)(uint32_t)-1;
    }
    return (uint64_t)(uint32_t)ctx->page_protect((uint32_t)user_page,
                                                 writable != 0u);
}

static uint64_t syscall_compat32_vm_release_page(void *ctx_ptr,
                                             uint64_t user_page) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx == 0 || user_page > 0xffffffffu) {
        return 0u;
    }
    return addrspace_release_page_with_backend(
        user_page,
        ctx->page_free,
        ctx->shared_page_unmap);
}

void syscall_compat32_vm_page_ops(
    struct syscall_compat32_context *ctx,
    struct syscall_common_vm_page_ops *ops) {
    if (ops == 0) {
        return;
    }
    ops->page_alloc = syscall_compat32_vm_page_alloc;
    ops->page_alloc_prot = syscall_compat32_vm_page_alloc_prot;
    ops->page_alloc_at = syscall_compat32_vm_page_alloc_at;
    ops->page_free = syscall_compat32_vm_page_free;
    ops->page_protect = syscall_compat32_vm_page_protect;
    ops->release_page = syscall_compat32_vm_release_page;
    ops->user_mmap_base = USER_MMAP_BASE;
    ops->user_mmap_end = USER_MMAP_END;
    ops->page_size = I386_VM_PAGE_SIZE;
    ops->max_pages = I386_MMAP_MAX_PAGES;
    ops->ctx = ctx;
}
