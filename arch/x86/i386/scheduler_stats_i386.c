#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/public/proc/sched_policy.h"
#include "scheduler_internal.h"

uint32_t i386_scheduler_ticks(void) {
    return scheduler_tick_count;
}

uint32_t i386_scheduler_switches(void) {
    return sched_runqueue_switch_count(&runqueue);
}

uint32_t i386_scheduler_completed(void) {
    return sched_runqueue_completed_count(&runqueue);
}

int i386_scheduler_policy_stats(struct sched_policy_stats *out) {
    return sched_policy_get_stats(out);
}

uint32_t i386_scheduler_quiet_tty_output(void) {
    return scheduler_quiet_tty_output;
}

uint32_t i386_scheduler_task_ticks(uint32_t task) {
    if (task >= I386_SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].ticks;
}

uint32_t i386_scheduler_task_root(uint32_t task) {
    if (task >= I386_SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].root;
}

uint32_t i386_scheduler_task_result(uint32_t task) {
    if (task >= I386_SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].result;
}

uint32_t i386_scheduler_current_pid(void) {
    if (!scheduler_active) {
        return 0;
    }
    return tasks[i386_scheduler_current_slot()].process.pid;
}

int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot) {
    const struct process *process;

    if (task >= I386_SCHEDULER_TASKS || snapshot == 0) {
        return 0;
    }
    process = &tasks[task].process;
    process_snapshot_fill(snapshot, process);
    return 1;
}
