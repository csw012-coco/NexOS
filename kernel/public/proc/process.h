#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"
#include "kernel/public/sys/system_limits.h"

struct tty;
struct address_space;
struct process_session;
struct user_page_mapping;
struct vfs;

enum process_exec_mode {
    PROCESS_EXEC_DIRECT = 0,
    PROCESS_EXEC_ELF = 1,
    PROCESS_EXEC_AUTO = 2
};

enum process_image_kind {
    PROCESS_IMAGE_NONE = 0,
    PROCESS_IMAGE_ELF = 1
};

enum process_state {
    PROCESS_STATE_FREE = 0,
    PROCESS_STATE_READY = 1,
    PROCESS_STATE_RUNNING = 2,
    PROCESS_STATE_SLEEPING = 3,
    PROCESS_STATE_STOPPED = 4,
    PROCESS_STATE_EXITED = 5,
    PROCESS_STATE_WAITING = 6
};

struct process;
struct process_snapshot {
    uint32_t pid;
    uint32_t slot;
    uint32_t state;
    int32_t exit_code;
    uint32_t wake_tick;
    uint32_t image_kind;
    char name[NOS_NAME_BUFFER_SIZE];
};

void process_init(struct tty *tty, volatile uint32_t *timer_ticks);
uint32_t process_program_count(void);
const char *process_program_name(uint32_t index);
int process_exec(struct vfs *vfs,
                 const char *name,
                 const char *const *envp,
                 enum process_exec_mode mode);
int process_run(const char *name);
int process_run_ring3_smoke_test(void);
const char *process_resolve_image_name(const char *name);
uint32_t process_last_error(void);
const struct process *process_current(void);
struct process *process_current_mut(void);
struct process_session *process_current_session(void);
struct user_page_mapping *process_current_mappings(void);
void process_exit_current(struct process_session *session, int32_t exit_code);
uint32_t process_capacity(void);
int process_get(uint32_t slot, struct process_snapshot *out);
int process_get_last_exit(struct process_snapshot *out);
int process_wait_last(struct process_snapshot *out);
int process_wait_pid(uint32_t pid, struct process_snapshot *out);
const char *process_cwd(const struct process *proc);
void process_set_cwd(struct process *proc, const char *path);
int process_exec_from_user(struct vfs *vfs,
                           struct process *proc,
                           char *command_line,
                           const char *const *envp);
int process_exec_replace_from_user(struct vfs *vfs,
                                   char *command_line,
                                   const char *const *envp);
int process_spawn_from_user(struct vfs *vfs,
                            struct process *proc,
                            char *command_line,
                            const char *const *envp,
                            uint32_t syscall_mode,
                            uint32_t flags);
