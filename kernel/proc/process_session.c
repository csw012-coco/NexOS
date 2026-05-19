#include "kernel/internal/proc/process_session_internal.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/scheduler.h"

static void session_restore_kernel_root(struct address_space *address_space) {
    if (address_space != 0 && address_space->user_cr3 != 0 && address_space->kernel_cr3 != 0) {
        (void)vmm_switch_root_or_fail(address_space->kernel_cr3);
    }
}

static int session_prepare_active_slice(struct process_session *session,
                                        struct user_page_mapping *mappings,
                                        struct process *proc,
                                        struct address_space *address_space,
                                        uint64_t entry,
                                        uint64_t stack_top,
                                        uint64_t *saved_kernel_rsp0_out) {
    uint64_t nested_kernel_rsp;
    uint32_t active_index;

    if (session == 0 || mappings == 0 || proc == 0 || saved_kernel_rsp0_out == 0 || entry == 0 || stack_top == 0) {
        return 0;
    }

    g_current_user_raw_entry = entry;
    proc->entry = entry;
    proc->stack_top = stack_top;
    if (address_space != 0 && address_space->user_cr3 != 0 &&
        !vmm_switch_root_or_fail(address_space->user_cr3)) {
        return 0;
    }
    *saved_kernel_rsp0_out = hal_kernel_stack_top();
    if (g_nested_kernel_stack_depth >= USER_PROCESS_LIMIT) {
        session_restore_kernel_root(address_space);
        return 0;
    }
    active_index = g_nested_kernel_stack_depth;
    g_active_sessions[active_index] = session;
    g_active_mappings[active_index] = mappings;
    nested_kernel_rsp =
        (uint64_t)(uintptr_t)&g_nested_kernel_stacks[g_nested_kernel_stack_depth][sizeof(g_nested_kernel_stacks[0])];
    g_nested_kernel_stack_depth++;
    hal_set_kernel_stack_top(nested_kernel_rsp);
    return 1;
}

static void session_complete_active_slice(struct address_space *address_space, uint64_t saved_kernel_rsp0) {
    if (g_nested_kernel_stack_depth != 0) {
        g_nested_kernel_stack_depth--;
        g_active_sessions[g_nested_kernel_stack_depth] = 0;
        g_active_mappings[g_nested_kernel_stack_depth] = 0;
    }
    hal_set_kernel_stack_top(saved_kernel_rsp0);
    session_restore_kernel_root(address_space);
}

static int session_should_continue(const struct process *proc) {
    return proc->state == PROCESS_STATE_READY || proc->state == PROCESS_STATE_STOPPED;
}

void session_finish(struct process_session *session, struct user_page_mapping *mappings) {
    struct process *proc;

    if (session == 0) {
        return;
    }
    proc = &session->process;
    process_bind_session(session, mappings);

    if (session->address_space.user_cr3 != 0) {
        if (!vmm_switch_root_or_fail(session->address_space.user_cr3)) {
            return;
        }
        addrspace_release_dynamic_pages();
        (void)vmm_switch_root_or_fail(session->address_space.kernel_cr3);
        session->address_space.user_cr3 = 0;
    } else {
        addrspace_release_dynamic_pages();
    }

    g_current_user_raw_entry = 0;
    job_clear_process_foreground_pid(proc);
    process_mark_exited(proc->address_space != 0 ? proc : 0, proc->exit_code);
    process_clear_current(session);
}

int session_enter_ring3(struct process_session *session,
                        struct user_page_mapping *mappings,
                        uint64_t entry,
                        uint64_t stack_top) {
    struct process *proc;

    if (session == 0) {
        return 0;
    }
    proc = &session->process;

    if (entry == 0 || stack_top == 0) {
        return 0;
    }

    g_current_user_raw_entry = entry;
    proc->entry = entry;
    proc->stack_top = stack_top;
    proc->state = PROCESS_STATE_RUNNING;
    proc->has_saved_frame = 0;
    proc->wake_tick = 0;
    job_set_process_foreground_pid(proc, proc->pid);

    for (;;) {
        if (!session_run_active_slice(session, mappings, entry, stack_top, 0)) {
            return 0;
        }
        if (proc->state == PROCESS_STATE_READY) {
            proc->state = PROCESS_STATE_WAITING;
            sched_tick();
            if (proc->state == PROCESS_STATE_WAITING) {
                proc->state = PROCESS_STATE_READY;
            }
            continue;
        }
        if (proc->state == PROCESS_STATE_SLEEPING) {
            while (proc->state == PROCESS_STATE_SLEEPING) {
                hal_cpu_halt();
            }
            continue;
        }
        if (proc->state == PROCESS_STATE_STOPPED) {
            break;
        }
        session_finish(session, mappings);
        break;
    }
    return 1;
}

int session_run_active_slice(struct process_session *session,
                             struct user_page_mapping *mappings,
                             uint64_t entry,
                             uint64_t stack_top,
                             int finish_on_exit) {
    struct process *proc;
    struct address_space *address_space;
    uint64_t saved_kernel_rsp0;

    if (session == 0) {
        return 0;
    }
    proc = &session->process;
    address_space = proc->address_space;

    if (!session_prepare_active_slice(session, mappings, proc, address_space, entry, stack_top, &saved_kernel_rsp0)) {
        return 0;
    }

    if (proc->has_saved_frame) {
        proc->state = PROCESS_STATE_RUNNING;
        hal_usermode_resume(&proc->saved_frame);
    } else {
        proc->state = PROCESS_STATE_RUNNING;
        hal_usermode_enter(entry, stack_top);
    }
    session_complete_active_slice(address_space, saved_kernel_rsp0);

    if (session_should_continue(proc)) {
        return 1;
    }
    if (proc->state != PROCESS_STATE_EXITED) {
        proc->state = PROCESS_STATE_EXITED;
    }
    if (finish_on_exit) {
        session_finish(session, mappings);
    }
    return 1;
}
