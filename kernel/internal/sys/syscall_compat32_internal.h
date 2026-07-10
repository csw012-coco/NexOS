#pragma once

#include <stdint.h>

#include "kernel/public/sys/syscall_compat32.h"

uint32_t syscall_compat32_open(struct syscall_compat32_context *ctx,
                               uint32_t user_path,
                               uint32_t flags);
uint32_t syscall_compat32_read(struct syscall_compat32_context *ctx,
                               uint32_t fd,
                               uint32_t user_address,
                               uint32_t size,
                               uint32_t flags);
uint32_t syscall_compat32_write(struct syscall_compat32_context *ctx,
                                uint32_t fd,
                                uint32_t user_address,
                                uint32_t size);
uint32_t syscall_compat32_close(struct syscall_compat32_context *ctx,
                                uint32_t fd);
uint32_t syscall_compat32_seek(struct syscall_compat32_context *ctx,
                               uint32_t fd,
                               int32_t offset,
                               uint32_t whence);
uint32_t syscall_compat32_page_alloc(struct syscall_compat32_context *ctx);
uint32_t syscall_compat32_page_free(struct syscall_compat32_context *ctx,
                                    uint32_t user_page);
uint32_t syscall_compat32_spawn(struct syscall_compat32_context *ctx,
                                uint32_t user_command,
                                uint32_t mode,
                                uint32_t flags);
uint32_t syscall_compat32_exec(struct syscall_compat32_context *ctx,
                               uint32_t user_command);
uintptr_t syscall_compat32_wait(struct syscall_compat32_context *ctx,
                                const struct process_context *context,
                                uint32_t pid,
                                int32_t *status,
                                int *blocked);
uintptr_t syscall_compat32_exit(struct syscall_compat32_context *ctx,
                                const struct process_context *context,
                                int exit_code);
uintptr_t syscall_compat32_yield(struct syscall_compat32_context *ctx,
                                 const struct process_context *context);
uintptr_t syscall_compat32_sleep(struct syscall_compat32_context *ctx,
                                 const struct process_context *context,
                                 uint32_t ticks);
uint32_t syscall_compat32_query(struct syscall_compat32_context *ctx,
                                uint32_t kind,
                                uint32_t arg0,
                                uint32_t arg1,
                                uint32_t user_info);
void syscall_compat32_vm_snapshot(struct syscall_compat32_context *ctx,
                                  struct syscall_vm_info *info);
void syscall_compat32_cleanup_pid(struct syscall_compat32_context *ctx,
                                  uint32_t pid);
uint32_t syscall_compat32_chdir(struct syscall_compat32_context *ctx,
                                uint32_t user_path);
uint32_t syscall_compat32_getcwd(struct syscall_compat32_context *ctx,
                                 uint32_t user_buffer,
                                 uint32_t size);
uint32_t syscall_compat32_opendir(struct syscall_compat32_context *ctx,
                                  uint32_t user_path);
uint32_t syscall_compat32_readdir(struct syscall_compat32_context *ctx,
                                  uint32_t fd,
                                  uint32_t user_entry);
uint32_t syscall_compat32_pipe(struct syscall_compat32_context *ctx,
                               uint32_t user_pair);
uint32_t syscall_compat32_mkdir(struct syscall_compat32_context *ctx,
                                uint32_t user_path);
uint32_t syscall_compat32_rmdir(struct syscall_compat32_context *ctx,
                                uint32_t user_path);
uint32_t syscall_compat32_remove(struct syscall_compat32_context *ctx,
                                 uint32_t user_path);
uint32_t syscall_compat32_mount(struct syscall_compat32_context *ctx,
                                uint32_t user_source,
                                uint32_t user_target,
                                uint32_t kind);
uint32_t syscall_compat32_umount(struct syscall_compat32_context *ctx,
                                 uint32_t user_target);
uint32_t syscall_compat32_dup2(struct syscall_compat32_context *ctx,
                               uint32_t src_fd,
                               uint32_t dst_fd);
uint32_t syscall_compat32_proc_query(struct syscall_compat32_context *ctx,
                                     uint32_t kind,
                                     uint32_t index,
                                     uint32_t user_info);
uint32_t syscall_compat32_kill(struct syscall_compat32_context *ctx,
                               uint32_t pid);

int syscall_compat32_request_core_io(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_fs(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_proc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_mount(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_mem(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_ipc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_core_misc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
