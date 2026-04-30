#pragma once

#include <stdint.h>

/**
 * sched_policy.h: Scheduler Policy Layer (SOSP-18: Mechanism Abstraction)
 * 
 * Separates scheduling POLICY (what) from MECHANISM (how):
 * 
 * Policy: Which process runs next, when wake up happens, priority decisions
 * Mechanism: Context switching, memory mapping, actual execution
 * 
 * This allows:
 * - Swapping algorithms without touching mechanism code
 * - Different policies per mode (interactive, batch, etc.)
 * - Testing policies independently
 */

/* Forward declarations */
struct job_runtime;

/**
 * Scheduling policy interface
 * 
 * The caller (scheduler_core.c) is the "mechanism" driver.
 * This layer (sched_policy.c) is the "policy" decision maker.
 */

/**
 * Initialize policy state at boot time
 */
void sched_policy_init(void);

/**
 * Update process wake times based on current timer.
 * Returns the number of processes that became ready.
 */
uint32_t sched_policy_update_wake_times(uint32_t current_ticks);

/**
 * Select the next ready process to run.
 * 
 * Implements the scheduling algorithm (FIFO, round-robin, priority, etc).
 * Returns the runtime slot of the next process, or -1 if none available.
 * 
 * This is the core policy decision point.
 */
int32_t sched_policy_select_next(void);

/**
 * Mark a process as needing reschedule
 * (Called after a process completes or enters sleep state)
 */
void sched_policy_on_process_finished(uint32_t slot);

/**
 * Query current scheduling statistics (optional, for debugging)
 */
struct sched_policy_stats {
    uint32_t total_processes;
    uint32_t ready_processes;
    uint32_t sleeping_processes;
    uint32_t current_slot;
};

int sched_policy_get_stats(struct sched_policy_stats *out);

/**
 * Set scheduling mode (future extensibility)
 * Modes: SCHED_MODE_INTERACTIVE (low latency), SCHED_MODE_BATCH (throughput)
 */
enum sched_mode {
    SCHED_MODE_INTERACTIVE = 0,  /* Current: responsiveness priority */
    SCHED_MODE_BATCH = 1          /* Future: throughput priority */
};

void sched_policy_set_mode(enum sched_mode mode);
