#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"

enum {
    SYSCALL_GFX_BATCH_CHUNK = 32
};

static struct syscall_gfx_batch_entry g_syscall_gfx_batch_entries[SYSCALL_GFX_BATCH_CHUNK];

static uint64_t syscall_gfx_copy_command(struct syscall_gfx_command *cmd, uint64_t user_info_addr) {
    if (!syscall_user_readable(user_info_addr, sizeof(*cmd))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(cmd, user_info_addr, sizeof(*cmd))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0u;
}

static uint64_t syscall_gfx_handle_batch(uint64_t user_info_addr) {
    struct syscall_gfx_batch batch;
    uint32_t processed = 0u;
    int valid = 1;

    if (!syscall_user_readable(user_info_addr, sizeof(batch)) ||
        !syscall_copy_from_user(&batch, user_info_addr, sizeof(batch))) {
        return syscall_kill_bad_user_pointer();
    }
    if (batch.count > SYS_GFX_BATCH_MAX_COMMANDS ||
        (batch.flags & ~SYS_GFX_BATCH_PRESENT) != 0u) {
        return (uint64_t)-1;
    }
    if (batch.count != 0u) {
        uint32_t bytes;

        if (batch.entries_addr == 0u ||
            batch.count > 0xffffffffu / sizeof(struct syscall_gfx_batch_entry)) {
            return (uint64_t)-1;
        }
        bytes = batch.count * sizeof(struct syscall_gfx_batch_entry);
        if (!syscall_user_readable(batch.entries_addr, bytes)) {
            return syscall_kill_bad_user_pointer();
        }
    }

    kernel_gfx_begin_batch();
    while (processed < batch.count) {
        uint32_t count = batch.count - processed;
        uint32_t bytes;

        if (count > SYSCALL_GFX_BATCH_CHUNK) {
            count = SYSCALL_GFX_BATCH_CHUNK;
        }
        bytes = count * sizeof(struct syscall_gfx_batch_entry);
        if (!syscall_copy_from_user(g_syscall_gfx_batch_entries,
                                    batch.entries_addr +
                                        (uint64_t)processed * sizeof(struct syscall_gfx_batch_entry),
                                    bytes)) {
            kernel_gfx_end_batch(0u);
            return syscall_kill_bad_user_pointer();
        }
        for (uint32_t i = 0; i < count; i++) {
            const struct syscall_gfx_batch_entry *entry = &g_syscall_gfx_batch_entries[i];

            if (entry->reserved != 0u ||
                entry->op == SYS_GFX_INFO ||
                entry->op == SYS_GFX_BATCH ||
                entry->op == SYS_GFX_PRESENT ||
                !kernel_gfx_dispatch(entry->op, &entry->command, 0)) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            break;
        }
        processed += count;
    }
    kernel_gfx_end_batch(valid ? batch.flags : 0u);
    return valid ? 0u : (uint64_t)-1;
}

uint64_t syscall_handle_gfx(uint32_t op, uint64_t user_info_addr) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind;

    if (op == SYS_GFX_BATCH) {
        return syscall_gfx_handle_batch(user_info_addr);
    }
    buffer_kind = kernel_gfx_buffer_kind(op);

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
