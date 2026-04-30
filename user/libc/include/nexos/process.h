#pragma once

#include "user/public/sysapi.h"

#define NEX_SPAWN_AUTO SYS_SPAWN_AUTO
#define NEX_SPAWN_ELF SYS_SPAWN_ELF
#define NEX_SPAWN_BACKGROUND SYS_SPAWN_BACKGROUND

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
#define NEX_WAIT_LAST_PID SYS_WAIT_LAST_PID

int exec(const char *name);
int spawn(const char *name, uint32_t mode, uint32_t flags);
int exec_replace(const char *name);
int proc_query(uint32_t kind, uint32_t index, struct syscall_process_info *info);
int wait(uint32_t pid, struct syscall_process_info *info);
int kill(uint32_t pid);
int fg(uint32_t pid);
int bg(uint32_t pid);
