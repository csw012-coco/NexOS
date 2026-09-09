#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"

/*
 * i386 VM compatibility helpers.
 *
 * These are still named compat32 because procfs/tests use the historical
 * symbols, but the implementation now delegates to the common address-space
 * model. Keep VM lifecycle policy out of the native int 0x40 request adapter.
 */

enum {
    COMPAT32_VM_PAGE_SIZE = 4096u
};

void syscall_compat32_vm_snapshot(struct syscall_compat32_context *ctx,
                                  struct syscall_vm_info *info) {
    if (info == 0) {
        return;
    }
    (void)ctx;
    process_mm_query_vm_snapshot(info);
}

void syscall_compat32_cleanup_pid(struct syscall_compat32_context *ctx,
                                  uint32_t pid) {
    if (pid == 0u) {
        return;
    }
    if (ctx != 0 && ctx->pid == pid) {
        addrspace_release_dynamic_pages_for_pid_with_backend(
            pid,
            ctx->page_free_pid,
            ctx->shared_page_unmap_pid);
    } else if (ctx != 0) {
        addrspace_release_shm_refs_for_pid(pid);
    } else {
        addrspace_release_dynamic_pages();
    }
}

int syscall_compat32_page_is_shared(uint32_t pid, uint32_t user_page) {
    uint32_t page = user_page & ~(COMPAT32_VM_PAGE_SIZE - 1u);

    (void)pid;
    return addrspace_page_is_shared(page);
}
