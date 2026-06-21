#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"

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

static uint64_t syscall_gfx_handle_blit(uint64_t user_info_addr) {
    struct syscall_gfx_blit blit;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t src_x = 0u;
    uint32_t src_y = 0u;
    uint32_t visible_width;
    uint32_t visible_height;
    uint64_t first_addr;
    uint64_t span;

    if (!syscall_user_readable(user_info_addr, sizeof(blit)) ||
        !syscall_copy_from_user(&blit, user_info_addr, sizeof(blit))) {
        return syscall_kill_bad_user_pointer();
    }
    if (blit.pixels_addr == 0u ||
        blit.width == 0u || blit.height == 0u ||
        blit.width > SYSCALL_GFX_BLIT_MAX_DIMENSION ||
        blit.height > SYSCALL_GFX_BLIT_MAX_DIMENSION ||
        blit.width > 0xffffffffu / sizeof(uint32_t) ||
        blit.pitch < blit.width * sizeof(uint32_t) ||
        blit.format != SYS_GFX_FORMAT_XRGB8888 ||
        blit.flags != 0u) {
        return (uint64_t)-1;
    }

    kernel_gfx_dimensions(&screen_width, &screen_height);
    if (screen_width == 0u || screen_height == 0u) {
        return (uint64_t)-1;
    }
    visible_width = blit.width;
    visible_height = blit.height;
    if (blit.dst_x < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)blit.dst_x);

        if (crop >= visible_width) {
            return 0u;
        }
        src_x = crop;
        visible_width -= crop;
        blit.dst_x = 0;
    }
    if (blit.dst_y < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)blit.dst_y);

        if (crop >= visible_height) {
            return 0u;
        }
        src_y = crop;
        visible_height -= crop;
        blit.dst_y = 0;
    }
    if ((uint32_t)blit.dst_x >= screen_width || (uint32_t)blit.dst_y >= screen_height) {
        return 0u;
    }
    if (visible_width > screen_width - (uint32_t)blit.dst_x) {
        visible_width = screen_width - (uint32_t)blit.dst_x;
    }
    if (visible_height > screen_height - (uint32_t)blit.dst_y) {
        visible_height = screen_height - (uint32_t)blit.dst_y;
    }

    first_addr = blit.pixels_addr +
                 (uint64_t)src_y * blit.pitch +
                 (uint64_t)src_x * sizeof(uint32_t);
    if (first_addr < blit.pixels_addr) {
        return (uint64_t)-1;
    }
    span = (uint64_t)(visible_height - 1u) * blit.pitch +
           (uint64_t)visible_width * sizeof(uint32_t);
    if (span == 0u || span > 0xffffffffu || first_addr + span < first_addr) {
        return (uint64_t)-1;
    }
    if (!syscall_user_readable(first_addr, (uint32_t)span)) {
        return syscall_kill_bad_user_pointer();
    }

    for (uint32_t x = 0u; x < visible_width;) {
        uint32_t chunk_width = visible_width - x;
        uint32_t row_bytes;
        uint32_t rows_per_chunk;

        if (chunk_width > SYSCALL_COPY_CHUNK / sizeof(uint32_t)) {
            chunk_width = SYSCALL_COPY_CHUNK / sizeof(uint32_t);
        }
        row_bytes = chunk_width * sizeof(uint32_t);
        rows_per_chunk = SYSCALL_COPY_CHUNK / row_bytes;
        for (uint32_t y = 0u; y < visible_height;) {
            uint32_t row_count = visible_height - y;

            if (row_count > rows_per_chunk) {
                row_count = rows_per_chunk;
            }
            for (uint32_t row = 0u; row < row_count; row++) {
                uint64_t row_addr = first_addr +
                                    (uint64_t)(y + row) * blit.pitch +
                                    (uint64_t)x * sizeof(uint32_t);

                if (!syscall_copy_from_user(&g_syscall_copy_buffer[row * row_bytes],
                                            row_addr,
                                            row_bytes)) {
                    return syscall_kill_bad_user_pointer();
                }
            }
            if (!kernel_gfx_blit_xrgb8888((const uint32_t *)g_syscall_copy_buffer,
                                          row_bytes,
                                          chunk_width,
                                          row_count,
                                          blit.dst_x + (int32_t)x,
                                          blit.dst_y + (int32_t)y)) {
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
