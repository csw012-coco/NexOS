#include "kernel/internal/proc/job_control_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/scheduler.h"
#include "lib/string.h"

enum job_terminal_kind {
    JOB_TERMINAL_NONE = 0,
    JOB_TERMINAL_TTY = 1,
    JOB_TERMINAL_SERIAL = 2
};

struct job_terminal_ref {
    uint8_t kind;
    struct tty *tty;
};

enum {
    JOB_TRACE_ENABLED = 0u
};

#define JOB_TRACE(...) do { \
    if (JOB_TRACE_ENABLED) { \
        kprint(__VA_ARGS__); \
    } \
} while (0)

static uint32_t g_serial_foreground_pid;

static int job_process_is_active(const struct process *proc) {
    return proc != 0 && proc->state != PROCESS_STATE_FREE && proc->state != PROCESS_STATE_EXITED;
}

static void job_capture_stop_frame(const struct syscall_frame *frame, struct process *proc) {
    if (frame == 0 || proc == 0 ||
        g_bound_session->process.image_kind == PROCESS_IMAGE_NONE ||
        g_bound_session->process.pid != proc->pid ||
        proc->state != PROCESS_STATE_RUNNING ||
        !hal_syscall_frame_is_user(frame)) {
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
    void *tty_handle;

    if (file == 0 || terminal_out == 0 || !file_is_active(file)) {
        return 0;
    }
    if (file->kind == KERNEL_FILE_VFS && file->vfs_node.mount_kind == VFS_MOUNT_DEVFS &&
        file->vfs_node.aux_index == VFS_DEV_TTYS0) {
        terminal_out->kind = JOB_TERMINAL_SERIAL;
        terminal_out->tty = 0;
        return 1;
    }
    tty_handle = file_tty_private_handle(file);
    if (tty_handle != 0) {
        terminal_out->kind = JOB_TERMINAL_TTY;
        terminal_out->tty = (struct tty *)tty_handle;
        return 1;
    }
    return 0;
}

static int job_exec_node_setuid_local(const struct vfs_node *node,
                                      uint32_t *uid_out,
                                      uint32_t *gid_out) {
    if (node == 0 || uid_out == 0 || gid_out == 0 ||
        node->kind != VFS_NODE_FILE ||
        node->mount_kind != VFS_MOUNT_NXFS ||
        (node->handle.nxfs_inode.mode & 04000u) == 0u) {
        return 0;
    }
    *uid_out = node->handle.nxfs_inode.uid;
    *gid_out = node->handle.nxfs_inode.gid;
    return 1;
}

static uint32_t job_apply_exec_identity_local(struct process *proc,
                                              const struct vfs_node *node,
                                              uint32_t base_caps) {
    uint32_t uid = 0u;
    uint32_t gid = 0u;
    /*
    kprint("SUID CHECK pid=%u kind=%u mount=%u mode=%o owner=%u:%u before=%u:%u\n",
           proc != 0 ? proc->pid : 0u,
           node != 0 ? node->kind : 0u,
           node != 0 ? node->mount_kind : 0u,
           node != 0 ? node->handle.nxfs_inode.mode : 0u,
           node != 0 ? node->handle.nxfs_inode.uid : 0u,
           node != 0 ? node->handle.nxfs_inode.gid : 0u,
           process_uid(proc),
           process_gid(proc));
    */
    if (proc != 0 && job_exec_node_setuid_local(node, &uid, &gid)) {
        (void)process_identity_push(proc);
        process_set_identity(proc, uid, gid);

        kprint("SUID APPLY pid=%u -> uid=%u gid=%u\n",
               proc->pid,
               process_uid(proc),
               process_gid(proc));

        if (uid == 0u) {
            return PROCESS_CAP_SYS_ADMIN;
        }
    }

    return base_caps;
}

static int job_process_waiting_on_tty(const struct process *proc, const struct tty *tty) {
    struct job_terminal_ref terminal;
    uint32_t foreground_pid;

    if (proc == 0 || tty == 0 || proc->state != PROCESS_STATE_WAITING) {
        return 0;
    }
    if (!job_file_terminal(&proc->files[SYS_FD_STDIN], &terminal)) {
        return 0;
    }
    if (terminal.kind != JOB_TERMINAL_TTY || terminal.tty != tty) {
        return 0;
    }
    foreground_pid = tty_foreground_pid(tty);
    return foreground_pid != 0u && foreground_pid == proc->pid;
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

void job_ensure_process_terminal_owner(const struct process *proc) {
    struct job_terminal_ref terminal;

    if (proc == 0 || proc->pid == 0u) {
        return;
    }
    terminal = job_process_terminal(proc);
    if (terminal.kind == JOB_TERMINAL_NONE) {
        return;
    }
    if (job_terminal_foreground_pid(terminal) == 0u) {
        job_terminal_set_foreground_pid(terminal, proc->pid);
    }
}

int job_claim_process_terminal(const struct process *proc) {
    struct job_terminal_ref terminal;

    if (proc == 0 || proc->pid == 0u) {
        return -NEX_ERR_INVAL;
    }
    terminal = job_process_terminal(proc);
    if (terminal.kind == JOB_TERMINAL_NONE) {
        return -NEX_ERR_INVAL;
    }
    job_terminal_set_foreground_pid(terminal, proc->pid);
    return 1;
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
        if (!g_job_runtimes[i].used) {
            continue;
        }
        if (job_process_waiting_on_tty(&g_job_runtimes[i].session.process, tty)) {
            g_job_runtimes[i].session.process.state = PROCESS_STATE_READY;
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
    uint64_t target_root = 0;

    if (session == 0 || mappings == 0) {
        job_bind_root_session();
        return;
    }
    process_bind_session(session, mappings);
    if (session->address_space.user_root != 0) {
        target_root = session->address_space.user_root;
    } else if (session->address_space.kernel_root != 0) {
        target_root = session->address_space.kernel_root;
    }
    if (target_root != 0 && !vmm_root_is_current(target_root)) {
        (void)vmm_switch_root_or_fail(target_root);
    }
}

static void job_cleanup_runtime(struct job_runtime *runtime) {
    if (runtime == 0) {
        return;
    }
    session_finish(&runtime->session, runtime->mappings);
    job_reset_runtime(runtime);
}

static void job_abort_spawn_slot(struct job_runtime *runtime,
                                 uint32_t slot,
                                 uint32_t pid) {
    struct process_snapshot ignored;

    if (runtime != 0 && runtime->used) {
        session_finish(&runtime->session, runtime->mappings);
        job_reset_runtime(runtime);
    }
    if (pid != 0u) {
        (void)process_wait_pid(pid, &ignored);
    }
    if (slot < USER_PROCESS_LIMIT && g_process_slot_used[slot]) {
        g_process_slot_used[slot] = 0;
        process_clear_slot_state(&g_process_slots[slot]);
    }
}

static void job_update_ready_work_while_foreground_waits(struct process_session *caller_session,
                                                         struct user_page_mapping *caller_mappings,
                                                         uint32_t foreground_pid) {
    sched_on_timer_tick(sched_current_ticks());
    if (foreground_pid != 0u) {
        sched_tick_excluding_pid(foreground_pid);
    }
    job_restore_bound_session(caller_session, caller_mappings);
}

static int job_prepare_foreground_terminal(uint32_t pid,
                                           struct job_runtime *runtime,
                                           struct process *caller_proc,
                                           struct job_terminal_ref *terminal_out,
                                           uint32_t *previous_pid_out) {
    struct job_terminal_ref terminal;
    struct job_terminal_ref runtime_terminal;

    if (runtime == 0 || terminal_out == 0 || previous_pid_out == 0) {
        return -NEX_ERR_INVAL;
    }

    runtime_terminal = job_process_terminal(&runtime->session.process);
    terminal = job_process_terminal(caller_proc != 0 ? caller_proc : &runtime->session.process);
    job_ensure_process_terminal_owner(caller_proc);
    if (terminal.kind == JOB_TERMINAL_NONE) {
        terminal = runtime_terminal;
    }
    if (terminal.kind != JOB_TERMINAL_NONE &&
        runtime_terminal.kind != JOB_TERMINAL_NONE &&
        !job_terminal_same(terminal, runtime_terminal)) {
        return -NEX_ERR_ACCES;
    }

    *terminal_out = terminal;
    *previous_pid_out = job_terminal_foreground_pid(terminal);
    job_terminal_set_foreground_pid(terminal, pid);
    return 1;
}

static void job_restore_foreground_terminal(struct process_session *caller_session,
                                            struct user_page_mapping *caller_mappings,
                                            struct job_terminal_ref terminal,
                                            uint32_t previous_pid) {
    job_restore_bound_session(caller_session, caller_mappings);
    job_terminal_set_foreground_pid(terminal, previous_pid);
}

static void job_run_foreground_slice(struct job_runtime *runtime) {
    process_bind_session(&runtime->session, runtime->mappings);
    if (!session_run_active_slice(&runtime->session,
                                  runtime->mappings,
                                  runtime->entry,
                                  runtime->stack_top,
                                  0)) {
        process_mark_exit_pending(&runtime->session.process,
                                  runtime->session.process.exit_code);
    }
}

static void job_finish_foreground_runtime_exit(struct job_runtime *runtime) {
    process_bind_session(&runtime->session, runtime->mappings);
    job_cleanup_runtime(runtime);
}

static int job_drive_foreground_runtime(struct job_runtime *runtime,
                                        struct process_session *caller_session,
                                        struct user_page_mapping *caller_mappings,
                                        uint32_t pid) {
    while (runtime->used && runtime->session.process.pid == pid) {
        switch (runtime->session.process.state) {
            case PROCESS_STATE_EXITED:
                job_finish_foreground_runtime_exit(runtime);
                return 1;
            case PROCESS_STATE_READY:
            case PROCESS_STATE_STOPPED:
                job_run_foreground_slice(runtime);
                if (runtime->session.process.state == PROCESS_STATE_STOPPED) {
                    return 1;
                }
                if (runtime->session.process.state == PROCESS_STATE_EXITED) {
                    job_finish_foreground_runtime_exit(runtime);
                    return 1;
                }
                job_update_ready_work_while_foreground_waits(caller_session, caller_mappings, pid);
                break;
            case PROCESS_STATE_FREE:
                job_reset_runtime(runtime);
                return 1;
            default:
                hal_display_service_pending();
                hal_cpu_wait_for_interrupt();
                job_update_ready_work_while_foreground_waits(caller_session, caller_mappings, pid);
                break;
        }
    }
    return 1;
}

static int job_start_runtime_session(struct job_runtime *runtime, struct process *proc, uint64_t kernel_root) {
    JOB_TRACE("job: runtime start pid=%u\n", proc != 0 ? proc->pid : 0u);
    runtime->used = 1;
    process_bind_session(&runtime->session, runtime->mappings);
    runtime->session.fpu_state_valid = 0u;
    runtime->session.process = *proc;
    process_forget_files(proc);
    process_discard_non_stdio_files(&runtime->session.process);
    runtime->session.process.image_kind = PROCESS_IMAGE_ELF;
    runtime->session.process.address_space = &runtime->session.address_space;
    runtime->session.process.state = PROCESS_STATE_READY;
    runtime->session.process.exit_code = 0;
    runtime->session.process.wake_tick = 0;
    runtime->session.address_space.kernel_root = kernel_root != 0 ? kernel_root : vmm_current_root();
    JOB_TRACE("job: runtime kernel_root=%lx current=%lx\n",
              runtime->session.address_space.kernel_root,
              vmm_current_root());
    if (!vmm_root_is_current(runtime->session.address_space.kernel_root) &&
        !vmm_switch_root_or_fail(runtime->session.address_space.kernel_root)) {
        g_process_slot_used[proc->slot] = 0;
        job_reset_runtime(runtime);
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    JOB_TRACE("job: runtime user root begin\n");
    runtime->session.address_space.user_root = vmm_create_user_root();
    JOB_TRACE("job: runtime user root=%lx\n", runtime->session.address_space.user_root);
    if (runtime->session.address_space.user_root == 0) {
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

int job_spawn_process(struct vfs *vfs,
                      const char *name,
                      const char *const *envp,
                      enum process_exec_mode mode,
                      uint32_t *pid_out) {
    char command_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_image_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_command_line[NOS_TTY_LINE_BUFFER_SIZE];
    struct vfs_node node;
    uint32_t bytes_read = 0;
    uint64_t entry = 0;
    struct process *proc;
    struct job_runtime *runtime;
    uint32_t allocated_slot;
    uint32_t allocated_pid;
    const char *image_name;
    struct process_session *caller_session = process_current_session();
    struct user_page_mapping *caller_mappings = process_current_mappings();
    const struct process *parent_proc = process_current();
    uint64_t caller_root = vmm_current_root();

    g_process_exec_last_error = PROCESS_EXEC_OK;
    JOB_TRACE("job: spawn start %s\n", name != 0 ? name : "(null)");
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
    JOB_TRACE("job: spawn command %s\n", command_name);
    image_name = command_name;
    JOB_TRACE("job: spawn resolve begin %s\n", image_name != 0 ? image_name : "(null)");
    if (!process_resolve_exec_target(vfs,
                                     image_name,
                                     name,
                                     resolved_image_name,
                                     sizeof(resolved_image_name),
                                     resolved_command_line,
                                     sizeof(resolved_command_line),
                                     &node,
                                     &bytes_read)) {
        return 0;
    }
    /*
    kprint("EXEC RESOLVE image=%s mode=%u owner=%u:%u\n",
       resolved_image_name,
       node.handle.nxfs_inode.mode,
       node.handle.nxfs_inode.uid,
       node.handle.nxfs_inode.gid);
    */
    JOB_TRACE("job: spawn resolve ok image=%s bytes=%u\n", resolved_image_name, bytes_read);
    JOB_TRACE("job: spawn caller root=%lx user=%lx kernel=%lx\n",
              caller_root,
              caller_session != 0 ? caller_session->address_space.user_root : 0,
              caller_session != 0 ? caller_session->address_space.kernel_root : 0);
    if (caller_session != 0 &&
        caller_session->process.image_kind == PROCESS_IMAGE_NONE &&
        caller_session->address_space.user_root == 0 &&
        caller_session->address_space.kernel_root == 0) {
        caller_session->address_space.kernel_root = caller_root;
    }

    job_ensure_process_terminal_owner(parent_proc);
    JOB_TRACE("job: spawn alloc slot begin\n");
    proc = process_alloc_slot(0, parent_proc);
    if (proc == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        return 0;
    }
    if (parent_proc == process_current()) {
        struct process *parent = process_current_mut();
        uint32_t base_caps =
            process_next_spawn_capabilities(parent, process_capabilities(parent));

        base_caps = job_apply_exec_identity_local(proc, &node, base_caps);
        process_set_capabilities(
            proc,
            process_exec_policy_capabilities(vfs, &node, resolved_image_name, base_caps));
        process_clear_next_spawn_capabilities(parent);
    }
    JOB_TRACE("job: spawn alloc slot ok pid=%u slot=%u\n", proc->pid, proc->slot);
    allocated_slot = proc->slot;
    allocated_pid = proc->pid;
    runtime = &g_job_runtimes[proc->slot];
    job_reset_runtime(runtime);
    if (!job_start_runtime_session(runtime,
                                   proc,
                                   caller_session != 0 ? caller_session->address_space.kernel_root : 0)) {
        job_abort_spawn_slot(runtime, allocated_slot, allocated_pid);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    JOB_TRACE("job: spawn runtime ok user_root=%lx\n", runtime->session.address_space.user_root);
    job_ensure_process_terminal_owner(&runtime->session.process);
    JOB_TRACE("job: spawn switch user root begin\n");
    if (!vmm_switch_root_or_fail(runtime->session.address_space.user_root)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        job_abort_spawn_slot(runtime, allocated_slot, allocated_pid);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    JOB_TRACE("job: spawn switch user root ok\n");
    addrspace_unmap_range_if_present(USER_ELF_BASE, USER_ELF_LIMIT);
    addrspace_unmap_range_if_present(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    vmm_allow_user_range(USER_ELF_BASE, USER_ELF_LIMIT);
    vmm_allow_user_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    process_set_name(&runtime->session.process, resolved_command_line);

    JOB_TRACE("job: spawn load elf begin\n");
    if (!process_load_elf_image(g_elf_file_buffer, bytes_read, &entry)) {
        job_abort_spawn_slot(runtime, allocated_slot, allocated_pid);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    JOB_TRACE("job: spawn load elf ok entry=%lx\n", entry);
    JOB_TRACE("job: spawn stack map begin\n");
    if (!addrspace_map_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        job_abort_spawn_slot(runtime, allocated_slot, allocated_pid);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    JOB_TRACE("job: spawn stack map ok\n");

    runtime->entry = entry;
    JOB_TRACE("job: spawn args begin\n");
    if (!process_prepare_arguments(resolved_command_line, envp, &runtime->stack_top)) {
        job_abort_spawn_slot(runtime, allocated_slot, allocated_pid);
        job_restore_bound_session(caller_session, caller_mappings);
        return 0;
    }
    JOB_TRACE("job: spawn args ok stack=%lx\n", runtime->stack_top);
    JOB_TRACE("job: spawn restore caller begin\n");
    job_restore_bound_session(caller_session, caller_mappings);
    JOB_TRACE("job: spawn restore caller ok current=%lx\n", vmm_current_root());
    g_process_exec_last_error = PROCESS_EXEC_OK;
    JOB_TRACE("job: spawn ok pid=%u\n", runtime->session.process.pid);
    if (pid_out != 0) {
        *pid_out = allocated_pid;
    }
    return 1;
}

int job_fork_current(const struct syscall_frame *frame, uint32_t *child_pid_out) {
    struct process_session *parent_session = process_current_session();
    struct user_page_mapping *parent_mappings = process_current_mappings();
    struct process *parent = process_current_mut();
    struct process *slot_proc;
    struct job_runtime *child;
    uint64_t child_root;

    if (frame == 0 || parent_session == 0 || parent_mappings == 0 || parent == 0 ||
        parent->image_kind == PROCESS_IMAGE_NONE ||
        parent_session->address_space.user_root == 0) {
        return 0;
    }
    slot_proc = process_alloc_slot(0, parent);
    if (slot_proc == 0) {
        return 0;
    }
    child = &g_job_runtimes[slot_proc->slot];
    job_reset_runtime(child);
    child_root = vmm_clone_root_cow(parent_session->address_space.user_root);
    if (child_root == 0) {
        g_process_slot_used[slot_proc->slot] = 0;
        process_clear_slot_state(slot_proc);
        return 0;
    }

    child->used = 1u;
    child->entry = parent->entry;
    child->stack_top = parent->stack_top;
    child->session.address_space = parent_session->address_space;
    child->session.address_space.user_root = child_root;
    child->session.address_space.reserved_phys_base = 0;
    child->session.address_space.reserved_phys_limit = 0;
    child->session.address_space.reserved_phys_next = 0;
    child->session.process = *slot_proc;
    process_forget_files(slot_proc);
    child->session.process.address_space = &child->session.address_space;
    child->session.process.image_kind = parent->image_kind;
    child->session.process.entry = parent->entry;
    child->session.process.stack_top = parent->stack_top;
    process_set_name(&child->session.process,
                     parent->name != 0 ? parent->name : "fork-child");
    child->session.process.saved_frame = *frame;
    child->session.process.saved_frame.rax = 0;
    child->session.process.has_saved_frame = 1u;
    child->session.process.state = PROCESS_STATE_READY;
    child->session.process.exit_code = 0;
    child->session.process.wake_tick = 0;
    child->session.fpu_state_valid = parent_session->fpu_state_valid;
    child->session.elf_image_size = parent_session->elf_image_size;
    child->session.elf_segment_count = parent_session->elf_segment_count;
    child->session.elf_backing_slot = parent_session->elf_backing_slot;
    memcpy(child->session.elf_segments,
           parent_session->elf_segments,
           sizeof(child->session.elf_segments));
    if (parent_session->elf_image_size != 0 &&
        !process_retain_elf_backing(&child->session)) {
        job_cleanup_runtime(child);
        return 0;
    }
    if (parent_session->fpu_state_valid) {
        memcpy(child->session.fpu_state,
               parent_session->fpu_state,
               sizeof(child->session.fpu_state));
    }
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        child->mappings[i] = parent_mappings[i];
        child->mappings[i].reserved_pool = 0;
    }
    if (!addrspace_fork_retain_shared(child_root, child->mappings)) {
        job_cleanup_runtime(child);
        return 0;
    }
    for (uint32_t i = 0; i < PROCESS_FILE_MAX; i++) {
        if (!file_is_active(&parent->files[i])) {
            continue;
        }
        if (!file_clone(&child->session.process.files[i], &parent->files[i])) {
            job_cleanup_runtime(child);
            return 0;
        }
    }
    if (child_pid_out != 0) {
        *child_pid_out = child->session.process.pid;
    }
    return 1;
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

int job_kill_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);

    if (runtime == 0) {
        return pid == 0u ? -NEX_ERR_INVAL : -NEX_ERR_SRCH;
    }
    if (runtime->session.process.state == PROCESS_STATE_EXITED) {
        return -NEX_ERR_CHILD;
    }
    if (runtime->session.process.state == PROCESS_STATE_FREE) {
        return -NEX_ERR_SRCH;
    }

    process_bind_session(&runtime->session, runtime->mappings);
    if (runtime->session.address_space.user_root != 0) {
        if (!vmm_switch_root_or_fail(runtime->session.address_space.user_root)) {
            job_bind_root_session();
            return -NEX_ERR_IO;
        }
    }
    process_exit_current(&runtime->session, -9);
    session_finish(&runtime->session, runtime->mappings);
    job_reset_runtime(runtime);
    job_bind_root_session();
    return 1;
}

int job_foreground_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);
    struct process_session *caller_session = process_current_session();
    struct user_page_mapping *caller_mappings = process_current_mappings();
    struct process *caller_proc = process_current_mut();
    struct job_terminal_ref terminal = job_terminal_none();
    uint32_t previous_foreground_pid = 0u;
    int rc;

    if (runtime == 0) {
        return pid == 0u ? -NEX_ERR_INVAL : -NEX_ERR_SRCH;
    }

    rc = job_prepare_foreground_terminal(pid,
                                         runtime,
                                         caller_proc,
                                         &terminal,
                                         &previous_foreground_pid);
    if (rc <= 0) {
        return rc;
    }

    rc = job_drive_foreground_runtime(runtime, caller_session, caller_mappings, pid);
    job_restore_foreground_terminal(caller_session,
                                    caller_mappings,
                                    terminal,
                                    previous_foreground_pid);
    return rc;
}

int job_background_pid(uint32_t pid) {
    struct job_runtime *runtime = job_find_runtime_by_pid(pid);

    if (runtime == 0) {
        return pid == 0u ? -NEX_ERR_INVAL : -NEX_ERR_SRCH;
    }
    if (runtime->session.process.state == PROCESS_STATE_EXITED) {
        return -NEX_ERR_CHILD;
    }
    if (runtime->session.process.state == PROCESS_STATE_FREE) {
        return -NEX_ERR_SRCH;
    }
    job_clear_process_foreground_pid(&runtime->session.process);
    if (runtime->session.process.state == PROCESS_STATE_STOPPED) {
        runtime->session.process.state = PROCESS_STATE_READY;
        runtime->session.process.wake_tick = 0;
    }
    return 1;
}

int job_current_process_foreground_allowed(void) {
    const struct process *proc = process_current();
    struct job_terminal_ref terminal;

    if (proc == 0) {
        return 0;
    }
    terminal = job_process_terminal(proc);
    return job_terminal_foreground_pid(terminal) == proc->pid;
}

int job_serial_current_process_foreground_allowed(void) {
    const struct process *proc = process_current();

    if (proc == 0) {
        return 1;
    }
    return g_serial_foreground_pid != 0u && g_serial_foreground_pid == proc->pid;
}

int job_tty_deliver_sigint(struct tty *tty) {
    struct job_terminal_ref terminal;
    struct process *proc;

    if (tty == 0) {
        return -NEX_ERR_INVAL;
    }
    terminal.kind = JOB_TERMINAL_TTY;
    terminal.tty = tty;
    proc = job_find_foreground_process(terminal);

    if (!job_process_is_active(proc)) {
        return -NEX_ERR_SRCH;
    }
    if (job_process_ignores_sigint(proc)) {
        return -NEX_ERR_ACCES;
    }

    process_mark_exit_pending(proc, 130);
    return 1;
}

int job_deliver_sigint_to_pid(uint32_t pid) {
    struct process *proc = 0;
    struct job_runtime *runtime;

    if (pid == 0u) {
        return -NEX_ERR_INVAL;
    }
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE &&
        g_user_session.process.pid == pid) {
        proc = &g_user_session.process;
    } else {
        runtime = job_find_runtime_by_pid(pid);
        if (runtime != 0) {
            proc = &runtime->session.process;
        }
    }
    if (!job_process_is_active(proc)) {
        return -NEX_ERR_SRCH;
    }
    if (job_process_ignores_sigint(proc)) {
        return -NEX_ERR_ACCES;
    }
    process_mark_exit_pending(proc, 130);
    return 1;
}

int job_tty_deliver_sigtstp(struct tty *tty, const struct syscall_frame *frame) {
    struct job_terminal_ref terminal;
    struct process *proc;

    if (tty == 0) {
        return -NEX_ERR_INVAL;
    }
    terminal.kind = JOB_TERMINAL_TTY;
    terminal.tty = tty;
    proc = job_find_foreground_process(terminal);

    if (!job_process_is_active(proc)) {
        return -NEX_ERR_SRCH;
    }
    if (job_process_ignores_sigint(proc)) {
        return -NEX_ERR_ACCES;
    }
    if (proc->state == PROCESS_STATE_STOPPED) {
        return 1;
    }

    if (hal_syscall_frame_is_user(frame)) {
        job_capture_stop_frame(frame, proc);
        proc->state = PROCESS_STATE_STOPPED;
        proc->wake_tick = 0;
    } else {
        proc->stop_pending = 1u;
        proc->wake_tick = 0;
    }
    return 1;
}
