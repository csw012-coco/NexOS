#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"

static uint64_t syscall_gfx_copy_command(struct syscall_gfx_command *cmd, uint64_t user_info_addr) {
    if (!syscall_user_readable(user_info_addr, sizeof(*cmd))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(cmd, user_info_addr, sizeof(*cmd))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0u;
}

uint64_t syscall_handle_gfx(uint32_t op, uint64_t user_info_addr) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind = kernel_gfx_buffer_kind(op);

    switch (buffer_kind) {
        case KERNEL_GFX_BUFFER_INFO_OUT:
            if (!syscall_user_writable(user_info_addr, sizeof(info))) {
                return syscall_kill_bad_user_pointer();
            }
            if (!kernel_gfx_dispatch(op, 0, &info)) {
                return (uint64_t)-1;
            }
            if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
                return syscall_kill_bad_user_pointer();
            }
            return 0u;
        case KERNEL_GFX_BUFFER_COMMAND_IN:
            if (syscall_gfx_copy_command(&cmd, user_info_addr) == SYSCALL_EXIT_TO_KERNEL) {
                return SYSCALL_EXIT_TO_KERNEL;
            }
            return kernel_gfx_dispatch(op, &cmd, 0) ? 0u : (uint64_t)-1;
        case KERNEL_GFX_BUFFER_INVALID:
        default:
            return (uint64_t)-1;
    }
}
