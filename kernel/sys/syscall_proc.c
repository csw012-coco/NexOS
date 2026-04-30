#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/proc/process_types_internal.h"

enum {
    SYSCALL_ENV_MAX = 16u,
    SYSCALL_ENV_TEXT_MAX = NOS_PATH_BUFFER_SIZE
};

struct syscall_env_capture {
    char storage[SYSCALL_ENV_MAX][SYSCALL_ENV_TEXT_MAX];
    const char *envp[SYSCALL_ENV_MAX + 1u];
};

static int syscall_copy_user_envp(struct syscall_env_capture *capture, uint64_t user_envp_addr) {
    uint32_t i;

    if (capture == 0) {
        return 0;
    }
    capture->envp[0] = 0;
    if (user_envp_addr == 0) {
        return 1;
    }

    for (i = 0; i < SYSCALL_ENV_MAX; i++) {
        uint64_t user_entry_addr = 0;

        if (!syscall_copy_from_user(&user_entry_addr,
                                    user_envp_addr + (uint64_t)i * sizeof(uint64_t),
                                    sizeof(user_entry_addr))) {
            return 0;
        }
        if (user_entry_addr == 0) {
            capture->envp[i] = 0;
            return 1;
        }
        if (!syscall_copy_user_cstr(capture->storage[i], user_entry_addr, sizeof(capture->storage[i]))) {
            return 0;
        }
        capture->envp[i] = capture->storage[i];
        capture->envp[i + 1u] = 0;
    }

    capture->envp[SYSCALL_ENV_MAX] = 0;
    return 1;
}

static void syscall_fill_process_info(struct syscall_process_info *out, const struct process_snapshot *proc) {
    uint32_t i;

    if (out == 0) {
        return;
    }
    out->pid = 0;
    out->slot = 0;
    out->state = PROCESS_STATE_FREE;
    out->exit_code = 0;
    out->wake_tick = 0;
    out->image_kind = PROCESS_IMAGE_NONE;
    for (i = 0; i < sizeof(out->name); i++) {
        out->name[i] = '\0';
    }
    if (proc == 0) {
        return;
    }
    out->pid = proc->pid;
    out->slot = proc->slot;
    out->state = proc->state;
    out->exit_code = proc->exit_code;
    out->wake_tick = proc->wake_tick;
    out->image_kind = proc->image_kind;
    for (i = 0; i < sizeof(out->name) - 1u && proc->name[i] != '\0'; i++) {
        out->name[i] = proc->name[i];
    }
}

static uint64_t syscall_write_process_info(uint64_t user_info_addr, const struct process_snapshot *proc) {
    struct syscall_process_info info;

    syscall_fill_process_info(&info, proc);
    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_exec(uint64_t user_name_addr, uint64_t user_envp_addr) {
    struct syscall_env_capture capture;
    struct process *proc = process_current_mut();

    if (proc == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_user_envp(&capture, user_envp_addr)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_exec_from_user(g_syscall_vfs, proc, g_syscall_name_buffer, capture.envp)) {
        return (uint64_t)(-(int64_t)process_last_error());
    }
    return 0;
}

uint64_t syscall_handle_exec_replace(uint64_t user_name_addr, uint64_t user_envp_addr) {
    struct syscall_env_capture capture;

    if (process_current_mut() == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_user_envp(&capture, user_envp_addr)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_exec_replace_from_user(g_syscall_vfs, g_syscall_name_buffer, capture.envp)) {
        return (uint64_t)(-(int64_t)process_last_error());
    }
    return 0;
}

uint64_t syscall_handle_spawn(uint64_t user_name_addr,
                              uint32_t syscall_mode,
                              uint32_t flags,
                              uint64_t user_envp_addr) {
    struct syscall_env_capture capture;
    struct process *proc = process_current_mut();

    if (proc == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_user_envp(&capture, user_envp_addr)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_spawn_from_user(g_syscall_vfs,
                                 proc,
                                 g_syscall_name_buffer,
                                 capture.envp,
                                 syscall_mode,
                                 flags)) {
        return (uint64_t)(-(int64_t)process_last_error());
    }
    return 0;
}

uint64_t syscall_handle_getpid(void) {
    const struct process *proc = process_current();

    return proc != 0 ? proc->pid : 0u;
}

uint64_t syscall_handle_proc_query(uint32_t kind, uint32_t index, uint64_t user_info_addr) {
    struct process_snapshot proc;
    int ok;

    if (!syscall_user_writable(user_info_addr, sizeof(struct syscall_process_info))) {
        return syscall_kill_bad_user_pointer();
    }

    switch (kind) {
        case SYS_PROC_QUERY_ALL:
            ok = process_get(index, &proc);
            break;
        case SYS_PROC_QUERY_JOBS:
            ok = job_get(index, &proc);
            break;
        case SYS_PROC_QUERY_LAST_EXIT:
            ok = process_get_last_exit(&proc);
            break;
        default:
            return 0;
    }
    if (!ok) {
        return 0;
    }
    return syscall_write_process_info(user_info_addr, &proc);
}

uint64_t syscall_handle_wait(uint32_t pid, uint64_t user_info_addr) {
    struct process_snapshot proc;
    int ok;

    if (!syscall_user_writable(user_info_addr, sizeof(struct syscall_process_info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (pid == SYS_WAIT_LAST_PID) {
        ok = process_wait_last(&proc);
    } else {
        ok = process_wait_pid(pid, &proc);
    }
    if (!ok) {
        return 0;
    }
    return syscall_write_process_info(user_info_addr, &proc);
}

uint64_t syscall_handle_kill(uint32_t pid) {
    return process_kill_pid(pid) ? 1u : 0u;
}

uint64_t syscall_handle_fg(uint32_t pid) {
    return job_foreground_pid(pid) ? 1u : 0u;
}

uint64_t syscall_handle_bg(uint32_t pid) {
    return job_background_pid(pid) ? 1u : 0u;
}
