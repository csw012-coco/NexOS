#include "kernel/internal/sched/scheduler_internal.h"
#include "kernel/public/proc/scheduler.h"
#include "kernel/public/proc/sched_policy.h"

static struct sched_trace_event g_sched_trace_events[SCHED_TRACE_EVENT_COUNT];
static uint32_t g_sched_trace_next;
static uint32_t g_sched_trace_count;

static void sched_trace_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (src == 0) {
        src = "(none)";
    }
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void sched_trace_record(const struct process *from,
                               const struct process *to,
                               const char *reason) {
    struct sched_trace_event *event = &g_sched_trace_events[g_sched_trace_next];

    event->tick = sched_current_ticks();
    event->from_pid = from != 0 ? from->pid : 0u;
    event->to_pid = to != 0 ? to->pid : 0u;
    event->from_state = from != 0 ? from->state : PROCESS_STATE_FREE;
    event->to_state = to != 0 ? to->state : PROCESS_STATE_FREE;
    sched_trace_copy_name(event->from_name, sizeof(event->from_name), from != 0 ? from->name : "(none)");
    sched_trace_copy_name(event->to_name, sizeof(event->to_name), to != 0 ? to->name : "(none)");
    sched_trace_copy_name(event->reason, sizeof(event->reason), reason);

    g_sched_trace_next = (g_sched_trace_next + 1u) % SCHED_TRACE_EVENT_COUNT;
    if (g_sched_trace_count < SCHED_TRACE_EVENT_COUNT) {
        g_sched_trace_count++;
    }
}

void sched_trace_snapshot(const struct sched_trace_event **events_out,
                          uint32_t *count_out,
                          uint32_t *next_out) {
    if (events_out != 0) {
        *events_out = g_sched_trace_events;
    }
    if (count_out != 0) {
        *count_out = g_sched_trace_count;
    }
    if (next_out != 0) {
        *next_out = g_sched_trace_next;
    }
}

void job_bind_foreground_session(void) {
    (void)session_bind_user_context(&g_user_session, g_user_page_mappings);
}

static void sched_restore_caller_session(void) {
    struct cpu_user_state *cpu_state = current_cpu_user_state();

    if (cpu_state->nested_kernel_stack_depth != 0) {
        uint32_t index = cpu_state->nested_kernel_stack_depth - 1u;

        if (cpu_state->active_sessions[index] != 0 &&
            cpu_state->active_mappings[index] != 0) {
            (void)session_bind_user_context(cpu_state->active_sessions[index],
                                            cpu_state->active_mappings[index]);
            return;
        }
    }
    job_bind_foreground_session();
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

    if (next_slot < -1) {
        sched_restore_caller_session();
        return;
    }

    /* MECHANISM: Execute foreground session (special case, slot = -1) */
    if (next_slot == -1) {
        if (g_user_session.process.state == PROCESS_STATE_READY) {
            sched_trace_record(process_current(), &g_user_session.process, "foreground");
            process_bind_session(&g_user_session, g_user_page_mappings);
            if (!session_run_active_slice(&g_user_session, g_user_page_mappings,
                                         g_user_session.process.entry,
                                         g_user_session.process.stack_top, 0)) {
                process_mark_exit_pending(&g_user_session.process, g_user_session.process.exit_code);
            }
            /* POLICY: Notify completion */
            sched_policy_on_process_finished((uint32_t)-1);
        }
        sched_restore_caller_session();
        return;
    }

    /* MECHANISM: Execute background job at slot */
    struct job_runtime *runtime = job_get_runtime((uint32_t)next_slot);

    if (runtime == 0) {
        sched_restore_caller_session();
        return;
    }

    sched_trace_record(process_current(), &runtime->session.process, "background");
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
    sched_restore_caller_session();
}

void sched_tick_excluding_pid(uint32_t pid) {
    sched_policy_set_excluded_pid(pid);
    sched_tick();
    sched_policy_set_excluded_pid(0);
}
