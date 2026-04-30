#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"

uint64_t syscall_handle_rtl8139_tx_test(void) {
    return kernel_rtl8139_send_test_frame() ? 1u : 0u;
}

uint64_t syscall_handle_rtl8139_tx_send(uint64_t user_info_addr) {
    struct syscall_rtl8139_tx_info info;
    uint8_t frame[1600];

    if (!syscall_user_readable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (info.bytes < 14u || info.bytes > sizeof(frame)) {
        return 0;
    }
    if (!syscall_user_readable(info.data_addr, info.bytes)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(frame, info.data_addr, info.bytes)) {
        return syscall_kill_bad_user_pointer();
    }
    return kernel_rtl8139_send_frame(frame, info.bytes) ? 1u : 0u;
}

uint64_t syscall_handle_rtl8139_rx_dump(uint64_t user_info_addr) {
    struct syscall_rtl8139_rx_info info;

    if (!syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_rtl8139_receive_packet(&info)) {
        return 0;
    }
    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}
