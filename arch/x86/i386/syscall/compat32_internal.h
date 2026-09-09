#pragma once

#include <stdint.h>

#include "arch/x86/i386/syscall/compat32.h"

struct syscall_common_vm_page_ops;
struct syscall_common_user_copy_ops;
struct syscall_common_file_io_ops;

uint32_t syscall_compat32_spawn(struct syscall_compat32_context *ctx,
                                uint32_t user_command,
                                uint32_t mode,
                                uint32_t flags);
uint32_t syscall_compat32_exec(struct syscall_compat32_context *ctx,
                               uint32_t user_command);
uintptr_t syscall_compat32_wait(struct syscall_compat32_context *ctx,
                                const struct process_context *context,
                                uint32_t pid,
                                uint32_t user_info,
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
void syscall_compat32_vm_snapshot(struct syscall_compat32_context *ctx,
                                  struct syscall_vm_info *info);
void syscall_compat32_cleanup_pid(struct syscall_compat32_context *ctx,
                                  uint32_t pid);
int syscall_compat32_page_is_shared(uint32_t pid, uint32_t user_page);
int syscall_compat32_copy_from_user(void *dest,
                                uint64_t user_addr,
                                uint32_t size);
int syscall_compat32_copy_to_user(uint64_t user_addr,
                              const void *src,
                              uint32_t size);
int syscall_compat32_copy_user_cstr(char *dest,
                                uint64_t user_addr,
                                uint32_t size);
void syscall_compat32_user_copy_ops(struct syscall_common_user_copy_ops *ops,
                                int copy_from_user,
                                int copy_to_user,
                                int copy_user_cstr,
                                uint64_t bad_pointer_value);
void syscall_compat32_vm_page_ops(
    struct syscall_compat32_context *ctx,
    struct syscall_common_vm_page_ops *ops);
void syscall_compat32_file_copy_ops(struct syscall_common_user_copy_ops *ops);
void syscall_compat32_file_io_ops(struct syscall_compat32_context *ctx,
                              struct syscall_common_file_io_ops *ops);
uint32_t syscall_compat32_fg(struct syscall_compat32_context *ctx,
                             uint32_t pid);
uint32_t syscall_compat32_bg(struct syscall_compat32_context *ctx,
                             uint32_t pid);
uint32_t syscall_compat32_proc_query(struct syscall_compat32_context *ctx,
                                     uint32_t kind,
                                     uint32_t index,
                                     uint32_t user_info);
uint32_t syscall_compat32_kill(struct syscall_compat32_context *ctx,
                               uint32_t pid);

int syscall_compat32_request_adapter_io(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_fs(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_proc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_mount(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_mem(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_ipc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_gfx(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_audio(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_audio_fd_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops);
int syscall_compat32_request_adapter_block(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_net(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_ui(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
int syscall_compat32_request_adapter_misc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
