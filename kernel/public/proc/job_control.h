#pragma once

#include <stdint.h>
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/syscall.h"

struct vfs;
struct tty;

/*
 * Job-control transition contract:
 *
 * job_spawn_process()
 *   FREE -> READY
 *   Creates a scheduler-visible job runtime and returns its pid through pid_out.
 *   It does not grant terminal ownership and does not wait.
 *
 * job_foreground_pid(pid)
 *   READY/STOPPED -> RUNNING/READY/WAITING/SLEEPING until STOPPED or EXITED
 *   Temporarily grants the pid terminal foreground ownership, runs/resumes it,
 *   then restores the previous foreground owner. It observes completion but
 *   does not reap; callers must still call process_wait_pid(pid).
 *
 * job_background_pid(pid)
 *   STOPPED -> READY, other live states unchanged
 *   Removes foreground ownership for pid and leaves it scheduler-visible.
 *
 * process_wait_pid(pid)
 *   EXITED -> FREE
 *   Reaps exactly one exited process. It is the normal slot-release boundary.
 *
 * job_kill_pid(pid), job_tty_deliver_sigint()
 *   live -> EXITED
 *
 * job_tty_deliver_sigtstp()
 *   live foreground -> STOPPED
 *
 * Job-control APIs return 1 on success or -NEX_ERR_*.
 */
int job_spawn_process(struct vfs *vfs,
                      const char *name,
                      const char *const *envp,
                      enum process_exec_mode mode,
                      uint32_t *pid_out);
int job_fork_current(const struct syscall_frame *frame, uint32_t *child_pid_out);
void job_inherit_stdio(struct process *proc);
uint32_t job_capacity(void);
int job_get(uint32_t slot, struct process_snapshot *out);
/* Returns 1 on success or -NEX_ERR_*. */
int job_kill_pid(uint32_t pid);
/* Returns 1 on success or -NEX_ERR_*. */
int job_foreground_pid(uint32_t pid);
/* Returns 1 on success or -NEX_ERR_*. */
int job_background_pid(uint32_t pid);
void job_ensure_process_terminal_owner(const struct process *proc);
/* Returns 1 after making proc the foreground owner of its terminal or -NEX_ERR_*. */
int job_claim_process_terminal(const struct process *proc);
int job_current_process_foreground_allowed(void);
int job_serial_current_process_foreground_allowed(void);
int job_tty_foreground_is_shell(struct tty *tty);
/* Returns 1 if delivered or -NEX_ERR_* if no signal was delivered. */
int job_tty_deliver_sigint(struct tty *tty);
/* Returns 1 if delivered to a live pid or -NEX_ERR_*. IRQ-safe: marks exit pending only. */
int job_deliver_sigint_to_pid(uint32_t pid);
/* Returns 1 if delivered or -NEX_ERR_* if no signal was delivered. */
int job_tty_deliver_sigtstp(struct tty *tty, const struct syscall_frame *frame);
int job_tty_wake_waiting_processes(struct tty *tty);
