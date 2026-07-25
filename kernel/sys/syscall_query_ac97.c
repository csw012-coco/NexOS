#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

uint64_t syscall_handle_ac97_query(uint64_t user_info_addr) {
    struct syscall_ac97_info info;

    if (!syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    (void)syscall_common_request_core_query_info(SYS_QUERY_AC97,
                                                 0u,
                                                 0u,
                                                 &info,
                                                 0);

    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_hda_query(uint64_t user_info_addr) {
    struct syscall_hda_info info;

    if (!syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    (void)syscall_common_request_core_query_info(SYS_QUERY_HDA,
                                                 0u,
                                                 0u,
                                                 &info,
                                                 0);

    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}
