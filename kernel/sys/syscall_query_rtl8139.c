#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"

uint64_t syscall_handle_rtl8139_query(uint64_t user_info_addr) {
    struct syscall_rtl8139_info info;

    if (!syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    kernel_query_rtl8139_info(&info);

    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}
