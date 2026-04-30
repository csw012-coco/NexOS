#pragma once

#include <stdint.h>
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/syscall.h"

struct vfs;

int job_run_background(struct vfs *vfs, const char *name83);
int job_run_background_with_pid(struct vfs *vfs,
                                const char *name,
                                const char *const *envp,
                                enum process_exec_mode mode,
                                uint32_t *pid_out);
void job_inherit_stdio(struct process *proc);
uint32_t job_capacity(void);
int job_get(uint32_t slot, struct process_snapshot *out);
int process_kill_pid(uint32_t pid);
int job_foreground_pid(uint32_t pid);
int job_background_pid(uint32_t pid);
void job_restore_foreground(void);
int job_tty_foreground_is_shell(void);
int job_tty_sigint(void);
int job_tty_sigtstp(const struct syscall_frame *frame);
