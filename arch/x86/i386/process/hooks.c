#include <stdint.h>
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/job_control.h"
#include "../scheduler/internal.h"

void process_wake_file_waiters(void *private_data, uint8_t file_kind) {
    if (private_data == 0) {
        return;
    }
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        (void)process_lifecycle_wake_file_waiter(&tasks[slot].process,
                                                 private_data,
                                                 file_kind);
    }
}

int job_tty_wake_waiting_processes(struct tty *tty) {
    int woke = 0;

    if (tty == 0) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        struct process *proc = &tasks[slot].process;

        if (proc->state == PROCESS_STATE_WAITING &&
            file_tty_private_handle(&proc->files[SYS_FD_STDIN]) == tty &&
            tty_foreground_pid(tty) == proc->pid) {
            proc->state = PROCESS_STATE_READY;
            woke = 1;
        }
    }
    return woke;
}
