#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"

static int syscall_prepare_user_output(uint64_t user_info_addr, uint32_t size) {
    if (!syscall_user_writable(user_info_addr, size)) {
        return 0;
    }
    return 1;
}

static uint64_t syscall_finish_user_output(uint64_t user_info_addr, const void *info, uint32_t size) {
    if (!syscall_copy_to_user(user_info_addr, info, size)) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_pci_query(uint64_t user_info_addr) {
    struct syscall_pci_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    kernel_query_pci_info(&info);
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}
