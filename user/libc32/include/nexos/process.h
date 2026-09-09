#pragma once

#include "user/public/sysapi.h"
#include <stdint.h>
#include <unistd.h>

#define NEX_PROC_QUERY_ALL SYS_PROC_QUERY_ALL
#define NEX_PROC_QUERY_JOBS SYS_PROC_QUERY_JOBS
#define NEX_PROC_QUERY_LAST_EXIT SYS_PROC_QUERY_LAST_EXIT

#define NEX_PROC_STATE_FREE SYS_PROC_STATE_FREE
#define NEX_PROC_STATE_READY SYS_PROC_STATE_READY
#define NEX_PROC_STATE_RUNNING SYS_PROC_STATE_RUNNING
#define NEX_PROC_STATE_SLEEPING SYS_PROC_STATE_SLEEPING
#define NEX_PROC_STATE_STOPPED SYS_PROC_STATE_STOPPED
#define NEX_PROC_STATE_EXITED SYS_PROC_STATE_EXITED
#define NEX_PROC_STATE_WAITING SYS_PROC_STATE_WAITING

#define NEX_PROC_IMAGE_NONE SYS_PROC_IMAGE_NONE
#define NEX_PROC_IMAGE_ELF SYS_PROC_IMAGE_ELF
#define NEX_PROC_SLOTS_MAX SYS_PROC_SLOTS_MAX

#define NEX_SPAWN_AUTO SYS_SPAWN_AUTO
#define NEX_SPAWN_ELF SYS_SPAWN_ELF
#define NEX_SPAWN_BACKGROUND SYS_SPAWN_BACKGROUND
#define NEX_WAIT_LAST_PID SYS_WAIT_LAST_PID

int proc_query(uint32_t kind, uint32_t index, struct syscall_process_info *info);
/*
 * spawn() returns a child pid (>0) or -NEX_ERR_* and never waits.
 * Foreground commands must run as spawn(..., 0), fg(pid), wait(pid, info).
 * Background commands use NEX_SPAWN_BACKGROUND and should be reaped later
 * with wait(pid, info).
 */
pid_t spawn(const char *command, uint32_t mode, uint32_t flags);
pid_t spawn_ex(const char *command, uint32_t mode, uint32_t flags);
/* Reaps an exited child/job and fills info. Returns 1 or -NEX_ERR_*. */
int wait(uint32_t pid, struct syscall_process_info *info);
/*
 * Legacy i386 surface: reaps a child and returns the raw child exit code.
 * Returns -NEX_ERR_* only when wait itself fails.
 */
int waitpid(pid_t pid);
int kill(pid_t pid);
/* Returns 1 on success or -NEX_ERR_*. */
int fg(uint32_t pid);
/* Returns 1 on success or -NEX_ERR_*. */
int bg(uint32_t pid);
/* Returns 1 after making the current process foreground owner of its tty. */
int tty_claim(void);
