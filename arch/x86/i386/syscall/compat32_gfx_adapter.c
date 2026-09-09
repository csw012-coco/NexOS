#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

enum {
    COMPAT32_GFX_BATCH_CHUNK = 16u,
    COMPAT32_GFX_BLIT_BUFFER_SIZE = 16384u,
    COMPAT32_GFX_BLIT_MAX_DIMENSION = 8192u
};

static struct syscall_gfx_batch_entry g_compat32_gfx_batch_entries[COMPAT32_GFX_BATCH_CHUNK];
static uint8_t g_compat32_gfx_blit_buffer[COMPAT32_GFX_BLIT_BUFFER_SIZE]
    __attribute__((aligned(sizeof(uint32_t))));

int syscall_compat32_request_adapter_gfx(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    (void)ctx;
    syscall_compat32_user_copy_ops(&copy_ops, 1, 1, 0, (uint64_t)-1);
    return syscall_common_request_core_gfx_request(
        request,
        result,
        &copy_ops,
        g_compat32_gfx_batch_entries,
        COMPAT32_GFX_BATCH_CHUNK,
        g_compat32_gfx_blit_buffer,
        COMPAT32_GFX_BLIT_BUFFER_SIZE,
        COMPAT32_GFX_BLIT_MAX_DIMENSION,
        0xffffffffu,
        0);
}
