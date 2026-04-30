#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/core/kprint.h"

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

uint64_t syscall_handle_kmsg_query(uint32_t offset, uint64_t user_info_addr) {
    struct syscall_kmsg_info info;
    uint32_t copied;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    info.total_size = kprint_log_size();
    info.offset = offset;
    info.bytes_copied = 0;
    for (uint32_t i = 0; i < sizeof(info.data); i++) {
        info.data[i] = '\0';
    }
    copied = kprint_log_read(offset, info.data, sizeof(info.data));
    if (copied == 0) {
        return 0;
    }
    info.bytes_copied = copied;
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}
