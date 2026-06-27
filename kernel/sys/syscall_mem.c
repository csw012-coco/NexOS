#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_elf_internal.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/core/kprint.h"

static int syscall_prepare_user_range(uint64_t user_addr, uint32_t size, int writable) {
    struct process_session *session = process_current_session();
    struct user_page_mapping *mappings = process_current_mappings();
    uint64_t end;
    uint64_t page;

    if (user_addr == 0u) {
        return 0;
    }
    if (size == 0u) {
        return 1;
    }
    end = user_addr + size;
    if (end < user_addr) {
        return 0;
    }
    page = user_addr & ~(uint64_t)(USER_PAGE_SIZE - 1u);
    while (page < end) {
        if (!vmm_user_readable(page, 1u) &&
            !process_handle_demand_page_fault(session,
                                              mappings,
                                              page,
                                              writable ? 0x6u : 0x4u)) {
            return 0;
        }
        if (writable && !vmm_user_writable(page, 1u)) {
            return 0;
        }
        page += USER_PAGE_SIZE;
    }
    return 1;
}

uint64_t syscall_kill_bad_user_pointer(void) {
    struct process_session *session = process_current_session();

    if (session != 0 && session->process.name != 0) {
        kprint("syscall: bad user pointer pid=%u name=%s\n", session->process.pid, session->process.name);
    } else if (session != 0) {
        kprint("syscall: bad user pointer pid=%u\n", session->process.pid);
    } else {
        kprint("syscall: bad user pointer pid=0\n");
    }
    process_exit_current(process_current_session(), -1);
    return SYSCALL_EXIT_TO_KERNEL;
}

int syscall_user_readable(uint64_t user_addr, uint32_t size) {
    return syscall_prepare_user_range(user_addr, size, 0) &&
           vmm_user_readable(user_addr, size);
}

int syscall_user_writable(uint64_t user_addr, uint32_t size) {
    return syscall_prepare_user_range(user_addr, size, 1) &&
           vmm_user_writable(user_addr, size);
}

int syscall_user_page_arg_valid(uint64_t user_addr) {
    return vmm_user_page_mapped(user_addr);
}

int syscall_copy_from_user(void *dest, uint64_t user_addr, uint32_t size) {
    return syscall_prepare_user_range(user_addr, size, 0) &&
           vmm_copy_from_user(dest, user_addr, size);
}

int syscall_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len) {
    uint32_t copied = 0u;

    if (dest == 0 || user_addr == 0u || max_len == 0u) {
        return 0;
    }
    while (copied + 1u < max_len) {
        uint64_t current = user_addr + copied;
        uint32_t page_remaining =
            USER_PAGE_SIZE - (uint32_t)(current & (USER_PAGE_SIZE - 1u));
        uint32_t buffer_remaining = max_len - 1u - copied;
        uint32_t chunk = page_remaining < buffer_remaining
            ? page_remaining
            : buffer_remaining;

        if (!syscall_prepare_user_range(current, chunk, 0) ||
            !vmm_copy_from_user(dest + copied, current, chunk)) {
            return 0;
        }
        for (uint32_t i = 0u; i < chunk; i++) {
            if (dest[copied + i] == '\0') {
                return 1;
            }
        }
        copied += chunk;
    }
    dest[max_len - 1u] = '\0';
    return 1;
}

int syscall_copy_to_user(uint64_t user_addr, const void *src, uint32_t size) {
    return syscall_prepare_user_range(user_addr, size, 1) &&
           vmm_copy_to_user(user_addr, src, size);
}

uint64_t syscall_handle_page_free(uint64_t user_page_addr) {
    if (!syscall_user_page_arg_valid(user_page_addr)) {
        return syscall_kill_bad_user_pointer();
    }
    return (uint64_t)addrspace_free_page(user_page_addr);
}
