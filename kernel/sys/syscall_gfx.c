#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

enum {
    SYSCALL_GFX_BATCH_CHUNK = 32,
    SYSCALL_GFX_BLIT_MAX_DIMENSION = 8192
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
    if (!syscall_common_request_core_gfx_batch_valid(&batch)) {
        return (uint64_t)-1;
    }
    if (batch.count != 0u) {
        uint32_t bytes;

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
        valid = syscall_common_request_core_gfx_batch_dispatch(
            g_syscall_gfx_batch_entries, count, 1);
        if (!valid) {
            break;
        }
        processed += count;
    }
    kernel_gfx_end_batch(valid ? batch.flags : 0u);
    return valid ? 0u : (uint64_t)-1;
}

static uint64_t syscall_gfx_handle_blit(uint64_t user_info_addr) {
    struct syscall_gfx_blit blit;
    struct syscall_common_gfx_blit_plan plan;
    int noop = 0;

    if (!syscall_user_readable(user_info_addr, sizeof(blit)) ||
        !syscall_copy_from_user(&blit, user_info_addr, sizeof(blit))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_common_request_core_gfx_blit_plan(
            &blit,
            SYSCALL_GFX_BLIT_MAX_DIMENSION,
            ~(uint64_t)0,
            &plan,
            &noop)) {
        return (uint64_t)-1;
    }
    if (noop) {
        return 0u;
    }
    if (!syscall_user_readable(plan.first_addr, (uint32_t)plan.span)) {
        return syscall_kill_bad_user_pointer();
    }

    for (uint32_t x = 0u; x < plan.visible_width;) {
        uint32_t chunk_width = plan.visible_width - x;
        uint32_t row_bytes;
        uint32_t rows_per_chunk;

        if (chunk_width > SYSCALL_COPY_CHUNK / sizeof(uint32_t)) {
            chunk_width = SYSCALL_COPY_CHUNK / sizeof(uint32_t);
        }
        row_bytes = chunk_width * sizeof(uint32_t);
        rows_per_chunk = SYSCALL_COPY_CHUNK / row_bytes;
        for (uint32_t y = 0u; y < plan.visible_height;) {
            uint32_t row_count = plan.visible_height - y;

            if (row_count > rows_per_chunk) {
                row_count = rows_per_chunk;
            }
            for (uint32_t row = 0u; row < row_count; row++) {
                uint64_t row_addr = plan.first_addr +
                                    (uint64_t)(y + row) * plan.pitch +
                                    (uint64_t)x * sizeof(uint32_t);

                if (!syscall_copy_from_user(&g_syscall_copy_buffer[row * row_bytes],
                                            row_addr,
                                            row_bytes)) {
                    return syscall_kill_bad_user_pointer();
                }
            }
            if (!syscall_common_request_core_gfx_blit_dispatch(
                    (const uint32_t *)g_syscall_copy_buffer,
                    row_bytes,
                    chunk_width,
                    row_count,
                    plan.dst_x + (int32_t)x,
                    plan.dst_y + (int32_t)y)) {
                return (uint64_t)-1;
            }
            y += row_count;
        }
        x += chunk_width;
    }
    return 0u;
}

uint64_t syscall_handle_gfx(uint32_t op, uint64_t user_info_addr) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind;

    if (op == SYS_GFX_BATCH) {
        return syscall_gfx_handle_batch(user_info_addr);
    }
    if (op == SYS_GFX_BLIT) {
        return syscall_gfx_handle_blit(user_info_addr);
    }
    buffer_kind = kernel_gfx_buffer_kind(op);

    switch (buffer_kind) {
        case KERNEL_GFX_BUFFER_INFO_OUT:
            if (!syscall_user_writable(user_info_addr, sizeof(info))) {
                return syscall_kill_bad_user_pointer();
            }
            if (!syscall_common_request_core_gfx_info(op, &info)) {
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
            return syscall_common_request_core_gfx_command(op, &cmd)
                ? 0u
                : (uint64_t)-1;
        case KERNEL_GFX_BUFFER_INVALID:
        default:
            return (uint64_t)-1;
    }
}
