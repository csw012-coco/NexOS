#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/core/kprint.h"

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
    return vmm_user_readable(user_addr, size);
}

int syscall_user_writable(uint64_t user_addr, uint32_t size) {
    return vmm_user_writable(user_addr, size);
}

int syscall_user_page_arg_valid(uint64_t user_addr) {
    return vmm_user_page_mapped(user_addr);
}

int syscall_copy_from_user(void *dest, uint64_t user_addr, uint32_t size) {
    return vmm_copy_from_user(dest, user_addr, size);
}

int syscall_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len) {
    return vmm_copy_user_cstr(dest, user_addr, max_len);
}

int syscall_copy_to_user(uint64_t user_addr, const void *src, uint32_t size) {
    return vmm_copy_to_user(user_addr, src, size);
}

uint64_t syscall_handle_page_free(uint64_t user_page_addr) {
    if (!syscall_user_page_arg_valid(user_page_addr)) {
        return syscall_kill_bad_user_pointer();
    }
    return (uint64_t)addrspace_free_page(user_page_addr);
}
