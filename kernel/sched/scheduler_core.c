#include "kernel/internal/sched/scheduler_internal.h"
#include "kernel/public/proc/sched_policy.h"

void job_bind_foreground_session(void) {
    (void)session_bind_user_context(&g_user_session, g_user_page_mappings);
}

void sched_prepare_user_return(void) {
    struct process_session *session;
    struct cpu_user_state *cpu_state;

    cpu_state = current_cpu_user_state();
    if (cpu_state->nested_kernel_stack_depth == 0) {
        return;
    }
    session = cpu_state->active_sessions[cpu_state->nested_kernel_stack_depth - 1];
    if (session == 0) {
        return;
    }
    session_prepare_user_return_context(session, cpu_state->active_mappings[cpu_state->nested_kernel_stack_depth - 1]);
}

uint64_t sched_prepare_user_frame_return(const struct syscall_frame *frame) {
    struct cpu_user_state *cpu_state;

    if (frame != 0 && (frame->cs & 0x3u) == 0x3u) {
        cpu_state = current_cpu_user_state();
        if (cpu_state->nested_kernel_stack_depth != 0) {
            uint32_t index = cpu_state->nested_kernel_stack_depth - 1u;

            if (session_prepare_user_frame_return(cpu_state->active_sessions[index],
                                                  cpu_state->active_mappings[index],
                                                  frame)) {
                return 1;
            }
            session_abort_user_frame_return(cpu_state->active_sessions[index],
                                            cpu_state->active_mappings[index]);
        }
        return 0;
    }
    sched_prepare_user_return();
    return 1;
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
                process_mark_exit_pending(&g_user_session.process, g_user_session.process.exit_code);
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
        process_mark_exit_pending(&runtime->session.process, runtime->session.process.exit_code);
    }
    if (runtime->session.process.state == PROCESS_STATE_EXITED) {
        session_finish(&runtime->session, runtime->mappings);
        job_reset_runtime(runtime);
        /* POLICY: Notify completion */
        sched_policy_on_process_finished((uint32_t)next_slot);
    }
    job_bind_foreground_session();
}
