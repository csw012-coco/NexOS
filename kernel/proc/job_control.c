#include "kernel/internal/proc/job_control_internal.h"
#include "fs/vfs.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/vmm.h"

static int job_process_is_active(const struct process *proc) {
    return proc != 0 && proc->state != PROCESS_STATE_FREE && proc->state != PROCESS_STATE_EXITED;
}

static void job_capture_stop_frame(const struct syscall_frame *frame, struct process *proc) {
    if (frame == 0 || proc == 0 ||
        g_bound_session->process.image_kind == PROCESS_IMAGE_NONE ||
        g_bound_session->process.pid != proc->pid ||
        proc->state != PROCESS_STATE_RUNNING) {
        return;
    }

    g_bound_session->process.saved_frame = *frame;
    g_bound_session->process.saved_frame.rax = 0;
    g_bound_session->process.has_saved_frame = 1;
}

static void job_restore_foreground_pid(uint32_t previous_tty_foreground_pid) {
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE) {
        job_set_tty_foreground_pid(g_user_session.process.pid);
        return;
    }
    job_set_tty_foreground_pid(previous_tty_foreground_pid);
}

static void job_restore_foreground_context(uint32_t previous_tty_foreground_pid) {
    job_bind_foreground_session();
    job_restore_foreground_pid(previous_tty_foreground_pid);
}

static void job_cleanup_runtime(struct job_runtime *runtime) {
    if (runtime == 0) {
        return;
    }
    session_finish(&runtime->session, runtime->mappings);
    job_reset_runtime(runtime);
}

static int job_start_runtime_session(struct job_runtime *runtime, struct process *proc) {
    runtime->used = 1;
    process_bind_session(&runtime->session, runtime->mappings);
    runtime->session.process = *proc;
    runtime->session.process.image_kind = PROCESS_IMAGE_ELF;
    runtime->session.process.address_space = &runtime->session.address_space;
    runtime->session.process.state = PROCESS_STATE_READY;
    runtime->session.process.exit_code = 0;
    runtime->session.process.wake_tick = 0;
    runtime->session.address_space.kernel_cr3 = vmm_current_root();
    runtime->session.address_space.user_cr3 = vmm_create_user_root();
    if (runtime->session.address_space.user_cr3 == 0) {
        g_process_slot_used[proc->slot] = 0;
        job_reset_runtime(runtime);
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    return 1;
}

static struct process *job_find_foreground_process(void) {
    struct job_runtime *runtime;

    if (g_tty_foreground_pid == 0) {
        return 0;
    }
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE &&
        g_user_session.process.pid == g_tty_foreground_pid) {
        return &g_user_session.process;
    }
    runtime = job_find_runtime_by_pid(g_tty_foreground_pid);
    if (runtime != 0) {
        return &runtime->session.process;
    }
    return 0;
}

int job_tty_foreground_is_shell(void) {
    struct process *proc = job_find_foreground_process();

    return job_process_is_active(proc) && job_process_ignores_sigint(proc);
}

int job_run_background_with_pid(struct vfs *vfs,
                                const char *name,
                                const char *const *envp,
                                enum process_exec_mode mode,
                                uint32_t *pid_out) {
    char command_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_image_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_command_line[NOS_TTY_LINE_BUFFER_SIZE];
    uint32_t bytes_read = 0;
    uint64_t entry = 0;
    struct process *proc;
    struct job_runtime *runtime;
    const char *image_name;

    g_process_exec_last_error = PROCESS_EXEC_OK;
    job_bind_foreground_session();
    if (vfs == 0 || name == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if (mode != PROCESS_EXEC_ELF && mode != PROCESS_EXEC_AUTO) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if (!process_extract_command_name(name, command_name, sizeof(command_name))) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    image_name = mode == PROCESS_EXEC_ELF ? command_name : process_resolve_image_name(command_name);
    if (!process_resolve_exec_target(vfs,
                                     image_name,
                                     name,
                                     resolved_image_name,
                                     sizeof(resolved_image_name),
                                     resolved_command_line,
                                     sizeof(resolved_command_line),
                                     0,
                                     &bytes_read)) {
        return 0;
    }

    proc = process_alloc_slot(0, g_user_session.process.image_kind != PROCESS_IMAGE_NONE ? &g_user_session.process : 0);
    if (proc == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        return 0;
    }
    if (pid_out != 0) {
        *pid_out = proc->pid;
    }
    runtime = &g_bg_runtimes[proc->slot];
    job_reset_runtime(runtime);
    if (!job_start_runtime_session(runtime, proc)) {
        job_bind_foreground_session();
        return 0;
    }
    if (!vmm_switch_root_or_fail(runtime->session.address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        job_cleanup_runtime(runtime);
        job_bind_foreground_session();
        return 0;
    }
    addrspace_unmap_range_if_present(USER_ELF_BASE, USER_ELF_LIMIT);
    addrspace_unmap_range_if_present(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    vmm_allow_user_range(USER_ELF_BASE, USER_ELF_LIMIT);
    vmm_allow_user_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    process_set_name(&runtime->session.process, resolved_image_name);

    if (!process_load_elf_image(g_elf_file_buffer, bytes_read, &entry)) {
        job_cleanup_runtime(runtime);
        job_bind_foreground_session();
        return 0;
    }
    if (!addrspace_map_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        job_cleanup_runtime(runtime);
        job_bind_foreground_session();
        return 0;
    }

    runtime->entry = entry;
    if (!process_prepare_arguments(resolved_command_line, envp, &runtime->stack_top)) {
        job_cleanup_runtime(runtime);
        job_bind_foreground_session();
        return 0;
    }
    job_bind_foreground_session();
    g_process_exec_last_error = PROCESS_EXEC_OK;
    return 1;
}

int job_run_background(struct vfs *vfs, const char *name83) {
    return job_run_background_with_pid(vfs, name83, 0, PROCESS_EXEC_AUTO, 0);
}

uint32_t job_capacity(void) {
    return USER_PROCESS_LIMIT;
}

int job_get(uint32_t slot, struct process_snapshot *out) {
    struct job_runtime *runtime;

    if (out == 0) {
        return 0;
    }
    runtime = job_get_runtime(slot);
    if (runtime == 0) {
        return 0;
    }
    process_snapshot_fill(out, &runtime->session.process);
    return 1;
}

int process_kill_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);

    if (runtime == 0) {
        return 0;
    }

    process_bind_session(&runtime->session, runtime->mappings);
    if (runtime->session.address_space.user_cr3 != 0) {
        if (!vmm_switch_root_or_fail(runtime->session.address_space.user_cr3)) {
            job_bind_foreground_session();
            return 0;
        }
    }
    process_exit_current(&runtime->session, -9);
    session_finish(&runtime->session, runtime->mappings);
    job_reset_runtime(runtime);
    job_bind_foreground_session();
    return 1;
}

int job_foreground_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);
    uint32_t previous_tty_foreground_pid = g_tty_foreground_pid;

    if (runtime == 0) {
        return 0;
    }

    job_set_tty_foreground_pid(pid);
    while (runtime->used && runtime->session.process.pid == pid) {
        if (runtime->session.process.state == PROCESS_STATE_EXITED) {
            process_bind_session(&runtime->session, runtime->mappings);
            job_cleanup_runtime(runtime);
            break;
        }
        if (runtime->session.process.state == PROCESS_STATE_READY ||
            runtime->session.process.state == PROCESS_STATE_STOPPED) {
            process_bind_session(&runtime->session, runtime->mappings);
            if (!session_run_active_slice(&runtime->session, runtime->mappings, runtime->entry, runtime->stack_top, 0)) {
                runtime->session.process.state = PROCESS_STATE_EXITED;
            }
            if (runtime->session.process.state == PROCESS_STATE_STOPPED) {
                job_restore_foreground_context(previous_tty_foreground_pid);
                break;
            }
            if (runtime->session.process.state == PROCESS_STATE_EXITED) {
                job_cleanup_runtime(runtime);
                break;
            }
            job_bind_foreground_session();
            continue;
        }
        if (runtime->session.process.state == PROCESS_STATE_FREE) {
            job_reset_runtime(runtime);
            break;
        }
        hal_cpu_halt();
    }

    job_restore_foreground_context(previous_tty_foreground_pid);
    return 1;
}

int job_background_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);

    if (runtime == 0) {
        return 0;
    }
    if (runtime->session.process.state == PROCESS_STATE_EXITED ||
        runtime->session.process.state == PROCESS_STATE_FREE) {
        return 0;
    }
    if (runtime->session.process.state == PROCESS_STATE_STOPPED) {
        runtime->session.process.state = PROCESS_STATE_READY;
        runtime->session.process.wake_tick = 0;
    }
    return 1;
}

int job_tty_sigint(void) {
    struct process *proc = job_find_foreground_process();

    if (!job_process_is_active(proc)) {
        return 0;
    }
    if (job_process_ignores_sigint(proc)) {
        return 0;
    }

    proc->exit_code = 130;
    proc->state = PROCESS_STATE_EXITED;
    proc->has_saved_frame = 0;
    proc->wake_tick = 0;
    return 1;
}

int job_tty_sigtstp(const struct syscall_frame *frame) {
    struct process *proc = job_find_foreground_process();

    if (!job_process_is_active(proc)) {
        return 0;
    }
    if (job_process_ignores_sigint(proc)) {
        return 0;
    }
    if (proc->state == PROCESS_STATE_STOPPED) {
        return 1;
    }

    job_capture_stop_frame(frame, proc);

    proc->state = PROCESS_STATE_STOPPED;
    proc->wake_tick = 0;
    return 1;
}
