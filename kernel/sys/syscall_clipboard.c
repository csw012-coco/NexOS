#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/clipboard_internal.h"

static uint32_t syscall_clipboard_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint64_t syscall_clipboard_copy_transfer_from_user(uint64_t user_info_addr,
                                                          struct syscall_clipboard_transfer *transfer) {
    if (transfer == 0 || !syscall_user_readable(user_info_addr, sizeof(*transfer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(transfer, user_info_addr, sizeof(*transfer))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0;
}

static uint64_t syscall_clipboard_copy_transfer_to_user(uint64_t user_info_addr,
                                                        const struct syscall_clipboard_transfer *transfer) {
    if (transfer == 0 || !syscall_user_writable(user_info_addr, sizeof(*transfer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_to_user(user_info_addr, transfer, sizeof(*transfer))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0;
}

static uint64_t syscall_clipboard_get(uint64_t user_info_addr) {
    struct syscall_clipboard_transfer transfer;
    const char *text;
    uint32_t total;
    uint32_t copied;
    uint64_t rc;

    rc = syscall_clipboard_copy_transfer_from_user(user_info_addr, &transfer);
    if (rc != 0) {
        return rc;
    }
    total = kernel_clipboard_size();
    copied = syscall_clipboard_min_u32(total, transfer.bytes);
    text = kernel_clipboard_text();
    if (copied > 0u) {
        if (!syscall_user_writable(transfer.data_addr, copied) ||
            !syscall_copy_to_user(transfer.data_addr, text, copied)) {
            return syscall_kill_bad_user_pointer();
        }
    }
    transfer.size = total;
    rc = syscall_clipboard_copy_transfer_to_user(user_info_addr, &transfer);
    return rc != 0 ? rc : copied;
}

static uint64_t syscall_clipboard_set(uint64_t user_info_addr) {
    struct syscall_clipboard_transfer transfer;
    char text[KERNEL_CLIPBOARD_TEXT_MAX + 1u];
    uint32_t bytes;
    uint64_t rc;

    rc = syscall_clipboard_copy_transfer_from_user(user_info_addr, &transfer);
    if (rc != 0) {
        return rc;
    }
    bytes = syscall_clipboard_min_u32(transfer.bytes, KERNEL_CLIPBOARD_TEXT_MAX);
    if (bytes > 0u) {
        if (!syscall_user_readable(transfer.data_addr, bytes) ||
            !syscall_copy_from_user(text, transfer.data_addr, bytes)) {
            return syscall_kill_bad_user_pointer();
        }
    }
    text[bytes] = '\0';
    transfer.size = kernel_clipboard_set_text(text, bytes);
    rc = syscall_clipboard_copy_transfer_to_user(user_info_addr, &transfer);
    return rc != 0 ? rc : transfer.size;
}

uint64_t syscall_handle_clipboard(uint32_t op, uint64_t user_info_addr) {
    struct syscall_clipboard_transfer transfer;
    uint64_t rc;

    switch (op) {
        case SYS_CLIPBOARD_GET:
            return syscall_clipboard_get(user_info_addr);
        case SYS_CLIPBOARD_SET:
            return syscall_clipboard_set(user_info_addr);
        case SYS_CLIPBOARD_CLEAR:
            return kernel_clipboard_set_text("", 0u);
        case SYS_CLIPBOARD_SIZE:
            rc = syscall_clipboard_copy_transfer_from_user(user_info_addr, &transfer);
            if (rc != 0) {
                return rc;
            }
            transfer.size = kernel_clipboard_size();
            rc = syscall_clipboard_copy_transfer_to_user(user_info_addr, &transfer);
            return rc != 0 ? rc : transfer.size;
        default:
            return (uint64_t)-1;
    }
}
