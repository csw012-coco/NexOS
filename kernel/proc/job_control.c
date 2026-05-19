#include "kernel/internal/proc/job_control_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/scheduler.h"

enum job_terminal_kind {
    JOB_TERMINAL_NONE = 0,
    JOB_TERMINAL_TTY = 1,
    JOB_TERMINAL_SERIAL = 2
};

struct job_terminal_ref {
    uint8_t kind;
    struct tty *tty;
};

static uint32_t g_serial_foreground_pid;

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

static struct job_terminal_ref job_terminal_none(void) {
    struct job_terminal_ref terminal;

    terminal.kind = JOB_TERMINAL_NONE;
    terminal.tty = 0;
    return terminal;
}

static int job_terminal_same(struct job_terminal_ref a, struct job_terminal_ref b) {
    return a.kind == b.kind && a.tty == b.tty;
}

static int job_tty_has_ready_input(const struct tty *tty) {
    return tty != 0 && (tty->line_ready != 0 || tty->char_count > 0);
}

static int job_file_terminal(const struct file *file, struct job_terminal_ref *terminal_out) {
    if (file == 0 || terminal_out == 0 || !file_is_active(file)) {
        return 0;
    }
    if ((file->kind == KERNEL_FILE_TTY_STDIN ||
         file->kind == KERNEL_FILE_TTY_STDOUT ||
         file->kind == KERNEL_FILE_TTY_STDERR) &&
        file->private_data != 0) {
        terminal_out->kind = JOB_TERMINAL_TTY;
        terminal_out->tty = (struct tty *)file->private_data;
        return 1;
    }
    if (file->kind == KERNEL_FILE_VFS && file->vfs_node.mount_kind == VFS_MOUNT_DEVFS) {
        if (file->vfs_node.aux_index == VFS_DEV_TTYS0) {
            terminal_out->kind = JOB_TERMINAL_SERIAL;
            terminal_out->tty = 0;
            return 1;
        }
        if ((file->vfs_node.aux_index == VFS_DEV_TTY ||
             file->vfs_node.aux_index == VFS_DEV_TTY2 ||
             file->vfs_node.aux_index == VFS_DEV_TTY3 ||
             file->vfs_node.aux_index == VFS_DEV_STDIN ||
             file->vfs_node.aux_index == VFS_DEV_STDOUT ||
             file->vfs_node.aux_index == VFS_DEV_STDERR) &&
            file->private_data != 0) {
            terminal_out->kind = JOB_TERMINAL_TTY;
            terminal_out->tty = (struct tty *)file->private_data;
            return 1;
        }
    }
    return 0;
}

static int job_process_waiting_on_tty(const struct process *proc, const struct tty *tty) {
    struct job_terminal_ref terminal;

    if (proc == 0 || tty == 0 || proc->state != PROCESS_STATE_WAITING) {
        return 0;
    }
    if (!job_file_terminal(&proc->files[SYS_FD_STDIN], &terminal)) {
        return 0;
    }
    return terminal.kind == JOB_TERMINAL_TTY && terminal.tty == tty;
}

static struct job_terminal_ref job_process_terminal(const struct process *proc) {
    struct job_terminal_ref terminal = job_terminal_none();

    if (proc == 0) {
        return terminal;
    }
    if (job_file_terminal(&proc->files[SYS_FD_STDIN], &terminal) ||
        job_file_terminal(&proc->files[SYS_FD_STDOUT], &terminal) ||
        job_file_terminal(&proc->files[SYS_FD_STDERR], &terminal)) {
        return terminal;
    }
    if (proc->console_handle != 0) {
        terminal.kind = JOB_TERMINAL_TTY;
        terminal.tty = (struct tty *)proc->console_handle;
    }
    return terminal;
}

static uint32_t job_terminal_foreground_pid(struct job_terminal_ref terminal) {
    if (terminal.kind == JOB_TERMINAL_TTY) {
        return tty_foreground_pid(terminal.tty);
    }
    if (terminal.kind == JOB_TERMINAL_SERIAL) {
        return g_serial_foreground_pid;
    }
    return 0;
}

static void job_terminal_set_foreground_pid(struct job_terminal_ref terminal, uint32_t pid) {
    if (terminal.kind == JOB_TERMINAL_TTY) {
        tty_set_foreground_pid(terminal.tty, pid);
    } else if (terminal.kind == JOB_TERMINAL_SERIAL) {
        g_serial_foreground_pid = pid;
    }
}

static void job_terminal_clear_foreground_pid(struct job_terminal_ref terminal, uint32_t pid) {
    if (pid == 0u) {
        return;
    }
    if (terminal.kind == JOB_TERMINAL_TTY) {
        tty_clear_foreground_pid(terminal.tty, pid);
    } else if (terminal.kind == JOB_TERMINAL_SERIAL && g_serial_foreground_pid == pid) {
        g_serial_foreground_pid = 0u;
    }
}

int job_tty_wake_waiting_processes(struct tty *tty) {
    int waked = 0;

    if (tty == 0 || !job_tty_has_ready_input(tty)) {
        return 0;
    }
    if (job_process_waiting_on_tty(&g_user_session.process, tty)) {
        g_user_session.process.state = PROCESS_STATE_READY;
        waked = 1;
    }
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        if (!g_bg_runtimes[i].used) {
            continue;
        }
        if (job_process_waiting_on_tty(&g_bg_runtimes[i].session.process, tty)) {
            g_bg_runtimes[i].session.process.state = PROCESS_STATE_READY;
            waked = 1;
        }
    }
    return waked;
}

void job_set_process_foreground_pid(const struct process *proc, uint32_t pid) {
    job_terminal_set_foreground_pid(job_process_terminal(proc), pid);
}

void job_clear_process_foreground_pid(const struct process *proc) {
    struct job_terminal_ref terminal;

    if (proc == 0) {
        return;
    }
    terminal = job_process_terminal(proc);
    job_terminal_clear_foreground_pid(terminal, proc->pid);
    if (terminal.kind == JOB_TERMINAL_TTY) {
        tty_set_raw_input(terminal.tty, 0);
    }
}

static void job_restore_bound_session(struct process_session *session, struct user_page_mapping *mappings) {
    if (session == 0 || mappings == 0) {
        job_bind_foreground_session();
        return;
    }
    process_bind_session(session, mappings);
    if (session->address_space.user_cr3 != 0) {
        (void)vmm_switch_root_or_fail(session->address_space.user_cr3);
    }
}

static void job_cleanup_runtime(struct job_runtime *runtime) {
    if (runtime == 0) {
        return;
    }
    session_finish(&runtime->session, runtime->mappings);
    job_reset_runtime(runtime);
}

static void job_yield_to_other_ready_work(struct process_session *caller_session,
                                          struct user_page_mapping *caller_mappings) {
    sched_tick();
    job_restore_bound_session(caller_session, caller_mappings);
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

static struct process *job_find_foreground_process(struct job_terminal_ref terminal) {
    struct job_runtime *runtime;
    uint32_t foreground_pid = job_terminal_foreground_pid(terminal);

    if (foreground_pid == 0) {
        return 0;
    }
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE &&
        g_user_session.process.pid == foreground_pid) {
        return &g_user_session.process;
    }
    runtime = job_find_runtime_by_pid(foreground_pid);
    if (runtime != 0) {
        return &runtime->session.process;
    }
    return 0;
}

int job_tty_foreground_is_shell(struct tty *tty) {
    struct job_terminal_ref terminal;
    struct process *proc;

    terminal.kind = JOB_TERMINAL_TTY;
    terminal.tty = tty;
    proc = job_find_foreground_process(terminal);

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
    struct process_session *caller_session = process_current_session();
    struct user_page_mapping *caller_mappings = process_current_mappings();
    const struct process *parent_proc = process_current();

    g_process_exec_last_error = PROCESS_EXEC_OK;
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

    proc = process_alloc_slot(0, parent_proc);
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
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    if (!vmm_switch_root_or_fail(runtime->session.address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        job_cleanup_runtime(runtime);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    addrspace_unmap_range_if_present(USER_ELF_BASE, USER_ELF_LIMIT);
    addrspace_unmap_range_if_present(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    vmm_allow_user_range(USER_ELF_BASE, USER_ELF_LIMIT);
    vmm_allow_user_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    process_set_name(&runtime->session.process, resolved_image_name);

    if (!process_load_elf_image(g_elf_file_buffer, bytes_read, &entry)) {
        job_cleanup_runtime(runtime);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    if (!addrspace_map_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        job_cleanup_runtime(runtime);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }

    runtime->entry = entry;
    if (!process_prepare_arguments(resolved_command_line, envp, &runtime->stack_top)) {
        job_cleanup_runtime(runtime);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    job_restore_bound_session(caller_session, caller_mappings);
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
    struct process_session *caller_session = process_current_session();
    struct user_page_mapping *caller_mappings = process_current_mappings();
    struct process *caller_proc = process_current_mut();
    struct job_terminal_ref terminal;
    struct job_terminal_ref runtime_terminal;
    uint32_t previous_foreground_pid;

    if (runtime == 0) {
        return 0;
    }
    runtime_terminal = job_process_terminal(&runtime->session.process);
    terminal = job_process_terminal(caller_proc != 0 ? caller_proc : &runtime->session.process);
    if (terminal.kind == JOB_TERMINAL_NONE) {
        terminal = runtime_terminal;
    }
    if (terminal.kind != JOB_TERMINAL_NONE &&
        runtime_terminal.kind != JOB_TERMINAL_NONE &&
        !job_terminal_same(terminal, runtime_terminal)) {
        return 0;
    }
    previous_foreground_pid = job_terminal_foreground_pid(terminal);

    job_terminal_set_foreground_pid(terminal, pid);
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
                job_restore_bound_session(caller_session, caller_mappings);
                job_terminal_set_foreground_pid(terminal, previous_foreground_pid);
                break;
            }
            if (runtime->session.process.state == PROCESS_STATE_EXITED) {
                job_cleanup_runtime(runtime);
                break;
            }
            job_yield_to_other_ready_work(caller_session, caller_mappings);
            continue;
        }
        if (runtime->session.process.state == PROCESS_STATE_FREE) {
            job_reset_runtime(runtime);
            break;
        }
        hal_cpu_halt();
        job_yield_to_other_ready_work(caller_session, caller_mappings);
    }

    job_restore_bound_session(caller_session, caller_mappings);
    job_terminal_set_foreground_pid(terminal, previous_foreground_pid);
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

int job_tty_sigint(struct tty *tty) {
    struct job_terminal_ref terminal;
    struct process *proc;

    terminal.kind = JOB_TERMINAL_TTY;
    terminal.tty = tty;
    proc = job_find_foreground_process(terminal);

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

int job_tty_sigtstp(struct tty *tty, const struct syscall_frame *frame) {
    struct job_terminal_ref terminal;
    struct process *proc;

    terminal.kind = JOB_TERMINAL_TTY;
    terminal.tty = tty;
    proc = job_find_foreground_process(terminal);

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
