#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/sys/syscall_native_request_core.h"

enum {
    SYSCALL_NATIVE_AUDIO_BUFFER_MAX = 1048576u
};

static uint8_t g_syscall_native_audio_buffer[SYSCALL_NATIVE_AUDIO_BUFFER_MAX];
static uint8_t g_syscall_native_rtl8139_tx_buffer[1600u];

static int syscall_native_copy_from_user_ops(void *dest,
                                             uint64_t user_addr,
                                             uint32_t size) {
    return syscall_user_readable(user_addr, size) &&
           syscall_copy_from_user(dest, user_addr, size);
}

static int syscall_native_copy_to_user_ops(uint64_t user_addr,
                                           const void *src,
                                           uint32_t size) {
    return syscall_user_writable(user_addr, size) &&
           syscall_copy_to_user(user_addr, src, size);
}

static int syscall_native_capability_event_copy_from_user(void *dest,
                                                          uint64_t user_addr,
                                                          uint32_t size) {
    return syscall_native_copy_from_user_ops(dest, user_addr, size);
}

static uint64_t syscall_native_misc_clear(void *ctx) {
    (void)ctx;
    return syscall_handle_clear();
}

static uint64_t syscall_native_misc_ticks(void *ctx) {
    (void)ctx;
    return g_syscall_ticks != 0 ? *g_syscall_ticks : 0u;
}

static uint64_t syscall_native_misc_reboot(void *ctx) {
    (void)ctx;
    return syscall_handle_reboot();
}

int syscall_native_request_core_io(const struct kernel_syscall_request *request,
                                   const struct syscall_frame *frame,
                                   struct kernel_syscall_result *result) {
    struct syscall_user_buffer buffer;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_OPEN:
            result->value =
                syscall_handle_open(kernel_syscall_arg_u64(request, 0),
                                    kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_READ:
            buffer.user_addr = kernel_syscall_arg_u64(request, 1);
            buffer.size = kernel_syscall_arg_u32(request, 2);
            result->value = syscall_handle_fd_read(
                kernel_syscall_arg_u32(request, 0),
                &buffer,
                kernel_syscall_arg_u32(request, 3),
                frame);
            return 1;
        case SYS_WRITE:
            buffer.user_addr = kernel_syscall_arg_u64(request, 1);
            buffer.size = kernel_syscall_arg_u32(request, 2);
            result->value = syscall_handle_fd_write(
                kernel_syscall_arg_u32(request, 0),
                &buffer,
                frame);
            return 1;
        case SYS_CLOSE:
            result->value =
                syscall_handle_close(kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_DUP2:
            result->value = syscall_handle_dup2(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_PIPE:
            result->value =
                syscall_handle_pipe(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_SEEK:
            result->value = syscall_handle_seek(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_i64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_fs(const struct kernel_syscall_request *request,
                            struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MKDIR:
            result->value =
                syscall_handle_mkdir(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_RMDIR:
            result->value =
                syscall_handle_rmdir(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_REMOVE:
            result->value =
                syscall_handle_remove(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_CHDIR:
            result->value =
                syscall_handle_chdir(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_GETCWD:
            result->value = syscall_handle_getcwd(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_OPENDIR:
            result->value =
                syscall_handle_opendir(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_READDIR:
            result->value = syscall_handle_readdir(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_MKFIFO:
            result->value =
                syscall_handle_mkfifo(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_SWITCH_ROOT:
            result->value =
                syscall_handle_switch_root(kernel_syscall_arg_u64(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_proc(const struct kernel_syscall_request *request,
                              const struct syscall_frame *frame,
                              struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_EXIT:
            process_exit_current(process_current_session(),
                                 kernel_syscall_arg_i32(request, 0));
            result->value = SYSCALL_EXIT_TO_KERNEL;
            result->action = SYSCALL_RESULT_EXIT;
            return 1;
        case SYS_YIELD:
            sched_yield_current(process_current_session(), frame);
            result->value = SYSCALL_EXIT_TO_KERNEL;
            result->action = SYSCALL_RESULT_YIELD;
            return 1;
        case SYS_SLEEP:
            sched_sleep_current(process_current_session(),
                                frame,
                                kernel_syscall_arg_u32(request, 0));
            result->value = SYSCALL_EXIT_TO_KERNEL;
            result->action = SYSCALL_RESULT_YIELD;
            return 1;
        case SYS_EXEC:
            result->value = syscall_handle_exec(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_EXEC_REPLACE:
            if (process_current() == 0) {
                result->value = (uint64_t)-1;
                return 1;
            }
            result->value = syscall_handle_exec_replace(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1));
            if ((int64_t)result->value >= 0) {
                result->value = SYSCALL_EXIT_TO_KERNEL;
                result->action = SYSCALL_RESULT_EXIT;
            }
            return 1;
        case SYS_WAIT:
            result->value = syscall_handle_wait(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_GETPID:
            result->value = syscall_handle_getpid();
            return 1;
        case SYS_PROC_QUERY:
            result->value = syscall_handle_proc_query(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u64(request, 2));
            return 1;
        case SYS_KILL:
            result->value =
                syscall_handle_kill(kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_FG:
            result->value =
                syscall_handle_fg(kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_BG:
            result->value =
                syscall_handle_bg(kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SPAWN:
            result->value = syscall_handle_spawn(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2),
                kernel_syscall_arg_u64(request, 3));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_mount(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MOUNT:
            result->value = syscall_handle_mount(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_UMOUNT:
            result->value =
                syscall_handle_umount(kernel_syscall_arg_u64(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_query(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0 || request->number != SYS_QUERY) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    if (syscall_common_request_core_query_request(request, result, &ops)) {
        return 1;
    }
    result->value = syscall_handle_query(kernel_syscall_arg_u32(request, 0),
                                         kernel_syscall_arg_u64(request, 1),
                                         kernel_syscall_arg_u64(request, 2),
                                         kernel_syscall_arg_u64(request, 3));
    return 1;
}

int syscall_native_request_core_mem(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MMAP:
            result->value =
                syscall_handle_mmap(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_MUNMAP:
            result->value = syscall_handle_munmap(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_PAGE_ALLOC:
            result->value = addrspace_alloc_page();
            return 1;
        case SYS_PAGE_FREE:
            result->value =
                syscall_handle_page_free(kernel_syscall_arg_u64(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_ipc(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_SHM_OPEN:
            result->value = syscall_handle_shm_open(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_SHM_UNLINK:
            result->value =
                syscall_handle_shm_unlink(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_MQ_OPEN:
            result->value = syscall_handle_mq_open(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_MQ_UNLINK:
            result->value =
                syscall_handle_mq_unlink(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_MQ_SEND:
            result->value = syscall_handle_mq_send(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_MQ_RECEIVE:
            result->value = syscall_handle_mq_receive(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_SEM_OPEN:
            result->value = syscall_handle_sem_open(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_SEM_UNLINK:
            result->value =
                syscall_handle_sem_unlink(kernel_syscall_arg_u64(request, 0));
            return 1;
        case SYS_SEM_TRYWAIT:
            result->value =
                syscall_handle_sem_trywait(kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEM_POST:
            result->value =
                syscall_handle_sem_post(kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_block(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_BLOCK_FLUSH:
            result->value = syscall_common_request_core_block_flush_dispatch(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_BLOCK_READ:
            result->value = syscall_handle_block_read(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u64(request, 2));
            return 1;
        case SYS_BLOCK_WRITE:
            result->value = syscall_handle_block_write(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u64(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_audio(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    if (syscall_common_request_core_audio_play_request(
            request,
            result,
            &ops,
            g_syscall_native_audio_buffer,
            sizeof(g_syscall_native_audio_buffer),
            (uint64_t)-1)) {
        return 1;
    }
    switch (request->number) {
        case SYS_AUDIO_TONE:
            return syscall_common_request_core_backend(request, result);
        case SYS_AUDIO_PLAY_FD:
            result->value = syscall_handle_audio_play_fd(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_net(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    if (syscall_common_request_core_rtl8139_request(
            request,
            result,
            &ops,
            g_syscall_native_rtl8139_tx_buffer,
            sizeof(g_syscall_native_rtl8139_tx_buffer),
            (uint64_t)-1)) {
        return 1;
    }
    switch (request->number) {
        case SYS_RTL8139_TX_TEST:
            return syscall_common_request_core_backend(request, result);
        default:
            return 0;
    }
}

int syscall_native_request_core_gfx(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_GFX:
            result->value = syscall_handle_gfx(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_GUI_EVENT:
            if (syscall_common_request_core_gui_event_request(
                    request,
                    result,
                    (uint32_t)syscall_handle_getpid(),
                    job_current_process_foreground_allowed())) {
                return 1;
            }
            result->value = syscall_handle_gui_event(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        case SYS_CLIPBOARD:
            if (syscall_common_request_core_clipboard(request, result)) {
                return 1;
            }
            result->value = syscall_handle_clipboard(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1));
            return 1;
        default:
            return 0;
    }
}

int syscall_native_request_core_misc(const struct kernel_syscall_request *request,
                              const struct syscall_frame *frame,
                              struct kernel_syscall_result *result) {
    struct syscall_common_misc_ops misc_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    misc_ops.clear = syscall_native_misc_clear;
    misc_ops.ticks = syscall_native_misc_ticks;
    misc_ops.reboot = syscall_native_misc_reboot;
    misc_ops.ctx = 0;
    if (syscall_common_request_core_misc_request(
            request, result, &misc_ops)) {
        return 1;
    }
    switch (request->number) {
        case SYS_FORK:
            result->value = syscall_handle_fork(frame);
            return 1;
        case SYS_CAPABILITY_EVENT:
            {
                struct syscall_common_user_input_ops ops;

                ops.copy_from_user = syscall_native_capability_event_copy_from_user;
                ops.bad_pointer = syscall_kill_bad_user_pointer;
                ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
                return syscall_common_request_core_capability_event_request(
                    request, result, &ops);
            }
        default:
            result->value = 0;
            return 1;
    }
}

int syscall_native_dispatch_request(
    const struct kernel_syscall_request *request,
    const struct syscall_frame *frame,
    struct kernel_syscall_result *result) {
    return syscall_native_request_core_io(request, frame, result) ||
           syscall_native_request_core_fs(request, result) ||
           syscall_native_request_core_proc(request, frame, result) ||
           syscall_native_request_core_mount(request, result) ||
           syscall_native_request_core_query(request, result) ||
           syscall_native_request_core_mem(request, result) ||
           syscall_native_request_core_ipc(request, result) ||
           syscall_native_request_core_block(request, result) ||
           syscall_native_request_core_audio(request, result) ||
           syscall_native_request_core_net(request, result) ||
           syscall_native_request_core_gfx(request, result) ||
           syscall_native_request_core_misc(request, frame, result);
}
