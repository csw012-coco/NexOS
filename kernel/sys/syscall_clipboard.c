#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

static char g_syscall_clipboard_buffer[KERNEL_CLIPBOARD_TEXT_MAX + 1u];

static int syscall_clipboard_copy_from_user(void *dest,
                                            uint64_t user_addr,
                                            uint32_t size) {
    return syscall_user_readable(user_addr, size) &&
           syscall_copy_from_user(dest, user_addr, size);
}

static int syscall_clipboard_copy_to_user(uint64_t user_addr,
                                          const void *src,
                                          uint32_t size) {
    return syscall_user_writable(user_addr, size) &&
           syscall_copy_to_user(user_addr, src, size);
}

uint64_t syscall_handle_clipboard(uint32_t op, uint64_t user_info_addr) {
    struct syscall_common_clipboard_transfer_ops ops;

    ops.copy_from_user = syscall_clipboard_copy_from_user;
    ops.copy_to_user = syscall_clipboard_copy_to_user;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    return syscall_common_request_core_clipboard_transfer(
        op,
        user_info_addr,
        &ops,
        g_syscall_clipboard_buffer,
        sizeof(g_syscall_clipboard_buffer));
}
