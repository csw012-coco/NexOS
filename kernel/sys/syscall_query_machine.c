#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/machine_info_internal.h"
#include "kernel/internal/core/system_query_internal.h"

static int syscall_prepare_machine_output(uint64_t user_info_addr, uint32_t size) {
    if (!syscall_user_writable(user_info_addr, size)) {
        return 0;
    }
    return 1;
}

static uint64_t syscall_finish_machine_output(uint64_t user_info_addr,
                                              const void *info,
                                              uint32_t size) {
    if (!syscall_copy_to_user(user_info_addr, info, size)) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_machine_info_query(uint64_t user_info_addr) {
    struct syscall_machine_info info;

    if (!syscall_prepare_machine_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }

    kernel_fill_machine_info(&info);
    return syscall_finish_machine_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_rtc_query(uint64_t user_info_addr) {
    struct syscall_rtc_info info;

    if (!syscall_prepare_machine_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_query_rtc_info(&info)) {
        return 0;
    }
    return syscall_finish_machine_output(user_info_addr, &info, sizeof(info));
}
