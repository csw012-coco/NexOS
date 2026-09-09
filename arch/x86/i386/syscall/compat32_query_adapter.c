#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/process.h"

static void syscall_compat32_query_copy_text(char *dst,
                                         uint32_t size,
                                         const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t syscall_compat32_query_fd_kind(void *ctx_ptr, uint32_t fd) {
    struct process *proc = process_current_mut();

    (void)ctx_ptr;
    if (proc == 0 || fd >= PROCESS_FILE_MAX) {
        return KERNEL_FILE_NONE;
    }
    return proc->files[fd].kind;
}

static int32_t syscall_compat32_query_fd_query(void *ctx_ptr,
                                           uint32_t fd,
                                           struct syscall_fd_info *info) {
    struct process *proc = process_current_mut();
    const struct file *file;

    (void)ctx_ptr;
    if (proc == 0 || info == 0 || fd >= PROCESS_FILE_MAX) {
        return 0;
    }
    file = &proc->files[fd];
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    info->fd = fd;
    info->kind = file->kind;
    info->flags = file->flags;
    info->offset = file->offset;
    info->node_kind = file->vfs_node.kind;
    info->mount_kind = file->vfs_node.mount_kind;
    info->readable = file_can_read(file) ? 1u : 0u;
    info->writable = file_can_write(file) ? 1u : 0u;
    syscall_compat32_query_copy_text(info->path,
                                 sizeof(info->path),
                                 file->opened_path);
    return file->kind != KERNEL_FILE_NONE ? 1 : 0;
}

static int syscall_compat32_query_fill_mount_info(
    void *ctx_ptr,
    struct syscall_mount_info *info,
    uint32_t index,
    uint32_t flags) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    return ctx != 0 && ctx->fill_mount_info != 0
        ? ctx->fill_mount_info(info, index, flags)
        : 0;
}

static void syscall_compat32_query_fill_machine_info(
    void *ctx_ptr,
    struct syscall_machine_info *info) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx != 0 && ctx->fill_machine_info != 0) {
        ctx->fill_machine_info(info);
    }
}

int syscall_compat32_request_adapter_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_query_ops query_ops;

    if (ctx == 0 || request == 0 || result == 0 ||
        request->number != SYS_QUERY) {
        return 0;
    }
    syscall_compat32_user_copy_ops(&copy_ops, 1, 1, 1, (uint64_t)(uint32_t)-1);
    query_ops.fd_kind = syscall_compat32_query_fd_kind;
    query_ops.tty_query = 0;
    query_ops.fd_query = syscall_compat32_query_fd_query;
    query_ops.fill_mount_info = syscall_compat32_query_fill_mount_info;
    query_ops.fill_machine_info = syscall_compat32_query_fill_machine_info;
    query_ops.ctx = ctx;
    return syscall_common_request_core_query_request_with_ops(
        request, result, &copy_ops, &query_ops);
}
