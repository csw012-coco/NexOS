#include "kernel/internal/sched/scheduler_internal.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/sched_policy.h"

void job_bind_foreground_session(void) {
    process_bind_session(&g_user_session, g_user_page_mappings);
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE &&
        g_user_session.address_space.user_cr3 != 0) {
        (void)vmm_switch_root_or_fail(g_user_session.address_space.user_cr3);
    }
}

void job_restore_foreground(void) {
    job_bind_foreground_session();
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE) {
        g_current_user_raw_entry = g_user_session.process.entry;
        job_set_process_foreground_pid(&g_user_session.process, g_user_session.process.pid);
    } else {
        job_clear_process_foreground_pid(&g_user_session.process);
    }
}

void sched_prepare_user_return(void) {
    struct process_session *session;
    const struct process *proc;

    if (g_nested_kernel_stack_depth == 0) {
        return;
    }
    session = g_active_sessions[g_nested_kernel_stack_depth - 1];
    if (session == 0) {
        return;
    }
    process_bind_session(session, g_active_mappings[g_nested_kernel_stack_depth - 1]);
    proc = &session->process;
    if (proc->image_kind == PROCESS_IMAGE_NONE || proc->address_space == 0) {
        return;
    }
    if (proc->address_space->user_cr3 != 0) {
        if (!vmm_switch_root_or_fail(proc->address_space->user_cr3)) {
            return;
        }
    }
    g_current_user_raw_entry = proc->entry;
}

void userprog_prepare_user_return(void) {
    sched_prepare_user_return();
}

void sched_on_timer_tick(uint32_t current_ticks) {
    /* POLICY is owned by the scheduler boundary, not IRQ dispatch. */
    (void)sched_policy_update_wake_times(current_ticks);
}

void sched_tick(void) {
    int32_t next_slot;

    /* POLICY: Update wake times for all sleeping processes */
    sched_on_timer_tick(sched_current_ticks());

    /* POLICY: Select next ready process to run */
    next_slot = sched_policy_select_next();

    /* MECHANISM: Execute foreground session (special case, slot = -1) */
    if (next_slot == -1) {
        if (g_user_session.process.state == PROCESS_STATE_READY) {
            process_bind_session(&g_user_session, g_user_page_mappings);
            if (!session_run_active_slice(&g_user_session, g_user_page_mappings,
                                         g_user_session.process.entry,
                                         g_user_session.process.stack_top, 0)) {
                g_user_session.process.state = PROCESS_STATE_EXITED;
            }
            /* POLICY: Notify completion */
            sched_policy_on_process_finished((uint32_t)-1);
        }
        job_bind_foreground_session();
        return;
    }

    /* MECHANISM: Execute background job at slot */
    struct job_runtime *runtime = job_get_runtime((uint32_t)next_slot);

    if (runtime == 0) {
        job_bind_foreground_session();
        return;
    }

    process_bind_session(&runtime->session, runtime->mappings);
    if (!session_run_active_slice(&runtime->session, runtime->mappings,
                                 runtime->entry, runtime->stack_top, 0)) {
        runtime->session.process.state = PROCESS_STATE_EXITED;
    }
    if (runtime->session.process.state == PROCESS_STATE_EXITED) {
        session_finish(&runtime->session, runtime->mappings);
        job_reset_runtime(runtime);
        /* POLICY: Notify completion */
        sched_policy_on_process_finished((uint32_t)next_slot);
    }
    job_bind_foreground_session();
}
