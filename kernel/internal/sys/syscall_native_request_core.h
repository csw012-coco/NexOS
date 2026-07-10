#pragma once

#include "kernel/public/sys/syscall.h"
#include "kernel/public/sys/syscall_request.h"

int syscall_native_dispatch_request(
    const struct kernel_syscall_request *request,
    const struct syscall_frame *frame,
    struct kernel_syscall_result *result);
int syscall_native_request_core_io(const struct kernel_syscall_request *request,
                                   const struct syscall_frame *frame,
                                   struct kernel_syscall_result *result);
int syscall_native_request_core_fs(const struct kernel_syscall_request *request,
                                   struct kernel_syscall_result *result);
int syscall_native_request_core_proc(const struct kernel_syscall_request *request,
                                     const struct syscall_frame *frame,
                                     struct kernel_syscall_result *result);
int syscall_native_request_core_mount(const struct kernel_syscall_request *request,
                                      struct kernel_syscall_result *result);
int syscall_native_request_core_query(const struct kernel_syscall_request *request,
                                      struct kernel_syscall_result *result);
int syscall_native_request_core_mem(const struct kernel_syscall_request *request,
                                    struct kernel_syscall_result *result);
int syscall_native_request_core_ipc(const struct kernel_syscall_request *request,
                                    struct kernel_syscall_result *result);
int syscall_native_request_core_block(const struct kernel_syscall_request *request,
                                      struct kernel_syscall_result *result);
int syscall_native_request_core_audio(const struct kernel_syscall_request *request,
                                      struct kernel_syscall_result *result);
int syscall_native_request_core_net(const struct kernel_syscall_request *request,
                                    struct kernel_syscall_result *result);
int syscall_native_request_core_gfx(const struct kernel_syscall_request *request,
                                    struct kernel_syscall_result *result);
int syscall_native_request_core_misc(const struct kernel_syscall_request *request,
                                     const struct syscall_frame *frame,
                                     struct kernel_syscall_result *result);
