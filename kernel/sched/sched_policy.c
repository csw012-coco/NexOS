#include "kernel/public/proc/sched_policy.h"
#include "kernel/internal/sched/scheduler_internal.h"
#include "kernel/public/proc/job_control.h"

/**
 * Scheduling Policy Implementation (SOSP-18: Policy-Mechanism Separation)
 * 
 * Current algorithm: Round-robin with tick-based wake-up
 * 
 * This file contains ALL policy decisions.
 * scheduler_core.c contains ONLY the mechanism (process binding, execution).
 */

/* Policy state */
static enum sched_mode g_sched_mode = SCHED_MODE_INTERACTIVE;
static uint32_t g_sched_next_slot = 0;

/* ============================================================================
 * Public Policy Interface
 * ========================================================================== */

void sched_policy_init(void) {
    g_sched_next_slot = 0;
    g_sched_mode = SCHED_MODE_INTERACTIVE;
}

/**
 * Check all sleeping processes and wake them up if their timer expired.
 * This is the "wake-up policy" - what conditions trigger becoming READY.
 */
uint32_t sched_policy_update_wake_times(uint32_t current_ticks) {
    uint32_t woke_up_count = 0;

    /* Check foreground session (always in slot 0 conceptually) */
    if (g_user_session.process.image_kind != PROCESS_IMAGE_NONE &&
        g_user_session.process.state == PROCESS_STATE_SLEEPING &&
        current_ticks >= g_user_session.process.wake_tick) {
        g_user_session.process.state = PROCESS_STATE_READY;
        g_user_session.process.wake_tick = 0;
        woke_up_count++;
    }

    /* Check background jobs */
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        struct job_runtime *runtime = job_get_runtime(i);

        if (runtime == 0) {
            continue;
        }
        if (runtime->session.process.state == PROCESS_STATE_SLEEPING &&
            current_ticks >= runtime->session.process.wake_tick) {
            runtime->session.process.state = PROCESS_STATE_READY;
            runtime->session.process.wake_tick = 0;
            woke_up_count++;
        }
    }

    return woke_up_count;
}

/**
 * Round-Robin Process Selection
 * 
 * This is THE core scheduling policy decision.
 * To change algorithm (priority queue, weighted round-robin, etc),
 * modify only this function and the data structures above.
 * 
 * Current: Simple round-robin with equal time slices.
 * Returns the slot index of next ready process, or -1 if none.
 */
int32_t sched_policy_select_next(void) {
    /* In interactive mode, check foreground first for responsiveness */
    if (g_sched_mode == SCHED_MODE_INTERACTIVE) {
        if (g_user_session.process.state == PROCESS_STATE_READY) {
            int32_t result = -1;  /* Foreground has special slot */
            g_sched_next_slot = 0;  /* Reset round-robin after foreground */
            return result;  /* Caller knows -1 = foreground session */
        }
    }

    /* Round-robin through background jobs (job slots 0 to USER_PROCESS_LIMIT-1) */
    for (uint32_t pass = 0; pass < USER_PROCESS_LIMIT; pass++) {
        uint32_t slot = (g_sched_next_slot + pass) % USER_PROCESS_LIMIT;
        struct job_runtime *runtime = job_get_runtime(slot);

        if (runtime == 0) {
            continue;
        }
        if (runtime->session.process.state != PROCESS_STATE_READY) {
            continue;
        }

        /* Found a ready process */
        g_sched_next_slot = (slot + 1) % USER_PROCESS_LIMIT;
        return (int32_t)slot;
    }

    /* No ready process found, return -1 */
    return -1;
}

/**
 * Called when a process finishes (EXITED state).
 * Policy can use this to update accounting or decision state.
 * Currently unused, but provides hook for future policies (e.g., priority decay).
 */
void sched_policy_on_process_finished(uint32_t slot) {
    (void)slot;  /* Currently no accounting, but extensible */
}

/**
 * Get current scheduling statistics (debug/monitoring)
 */
int sched_policy_get_stats(struct sched_policy_stats *out) {
    uint32_t ready = 0, sleeping = 0;

    if (!out) {
        return 0;
    }

    if (g_user_session.process.state == PROCESS_STATE_READY) {
        ready++;
    } else if (g_user_session.process.state == PROCESS_STATE_SLEEPING) {
        sleeping++;
    }

    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        struct job_runtime *runtime = job_get_runtime(i);

        if (runtime == 0) {
            continue;
        }
        if (runtime->session.process.state == PROCESS_STATE_READY) {
            ready++;
        } else if (runtime->session.process.state == PROCESS_STATE_SLEEPING) {
            sleeping++;
        }
    }

    out->total_processes = 1 + USER_PROCESS_LIMIT;  /* foreground + background */
    out->ready_processes = ready;
    out->sleeping_processes = sleeping;
    out->current_slot = g_sched_next_slot;
    return 1;
}

/**
 * Set scheduling mode.
 * This allows swapping policies at runtime.
 * 
 * Future: Could add more modes:
 *   - SCHED_MODE_REALTIME: Fixed priorities
 *   - SCHED_MODE_FAIR: Weighted fair queueing
 */
void sched_policy_set_mode(enum sched_mode mode) {
    if (mode < 2) {  /* Valid mode range */
        g_sched_mode = mode;
    }
}
