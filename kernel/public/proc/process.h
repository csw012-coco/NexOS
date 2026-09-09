#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"
#include "kernel/public/sys/system_limits.h"

struct tty;
struct address_space;
struct process_session;
struct user_page_mapping;
struct vfs;
struct vfs_node;

enum process_exec_mode {
    PROCESS_EXEC_DIRECT = 0,
    PROCESS_EXEC_ELF = 1,
    PROCESS_EXEC_AUTO = 2
};

enum process_image_kind {
    PROCESS_IMAGE_NONE = 0,
    PROCESS_IMAGE_ELF = 1
};

enum process_capability {
    PROCESS_CAP_POWER = 1u << 0,
    PROCESS_CAP_RAW_BLOCK = 1u << 1,
    PROCESS_CAP_MOUNT = 1u << 2,
    PROCESS_CAP_SIGNAL = 1u << 3,
    PROCESS_CAP_GRANT = 1u << 4,
    PROCESS_CAP_AUDIO = 1u << 5,
    PROCESS_CAP_NET_RAW = 1u << 6,
    PROCESS_CAP_DISPLAY = 1u << 7,
    PROCESS_CAP_INPUT = 1u << 8,
    PROCESS_CAP_CLIPBOARD = 1u << 9,
    PROCESS_CAP_DEBUG = 1u << 10,
    PROCESS_CAP_SYS_ADMIN =
        PROCESS_CAP_POWER |
        PROCESS_CAP_RAW_BLOCK |
        PROCESS_CAP_MOUNT |
        PROCESS_CAP_SIGNAL |
        PROCESS_CAP_GRANT |
        PROCESS_CAP_AUDIO |
        PROCESS_CAP_NET_RAW |
        PROCESS_CAP_DISPLAY |
        PROCESS_CAP_INPUT |
        PROCESS_CAP_CLIPBOARD |
        PROCESS_CAP_DEBUG
};

/*
 * Process state contract:
 *
 * FREE      Slot has no live or reapable process. Only alloc/spawn may leave it.
 * READY     Runnable and scheduler-visible. Scheduler/synchronous fg may run it.
 * RUNNING   Currently executing in user mode or about to return there.
 * SLEEPING  Timer-blocked; wake_tick owns the READY transition.
 * STOPPED   Job-control stop (^Z). Not scheduler-visible until bg/fg resumes it.
 * EXITED    Finished but not reaped. wait(pid) is the only normal FREE transition.
 * WAITING   Blocked in a kernel wait/read/write path; the blocking owner wakes it.
 *
 * Normal transitions:
 *   FREE -> READY              spawn/fork/exec runtime creation
 *   READY -> RUNNING           scheduler or foreground runner
 *   RUNNING -> READY           yield, preemption, zero sleep, syscall resume
 *   RUNNING -> SLEEPING        timed sleep
 *   SLEEPING -> READY          timer wake
 *   RUNNING/READY -> WAITING   blocking syscall or foreground parent wait
 *   WAITING -> READY           I/O/event/child wake
 *   RUNNING/READY -> STOPPED   job-control stop
 *   STOPPED -> READY           fg(pid) or bg(pid)
 *   any live state -> EXITED   exit, kill, fatal run failure, SIGINT
 *   EXITED -> FREE             wait(pid) reap
 */
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
    uint32_t caps;
    uint32_t uid;
    uint32_t gid;
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
uint32_t process_capabilities(const struct process *proc);
void process_set_capabilities(struct process *proc, uint32_t caps);
int process_has_capability(const struct process *proc, uint32_t cap);
uint32_t process_uid(const struct process *proc);
uint32_t process_gid(const struct process *proc);
void process_set_identity(struct process *proc,
                          uint32_t uid,
                          uint32_t gid);
int process_identity_push(struct process *proc);
int process_identity_pop(struct process *proc);
void process_inherit_identity(struct process *proc, const struct process *parent);
uint32_t process_exec_policy_capabilities(struct vfs *vfs,
                                          const struct vfs_node *node,
                                          const char *image_name,
                                          uint32_t base_caps);
uint32_t process_next_spawn_capabilities(const struct process *proc,
                                         uint32_t fallback);
int process_next_spawn_capabilities_enabled(const struct process *proc);
void process_set_next_spawn_capabilities(struct process *proc, uint32_t caps);
void process_clear_next_spawn_capabilities(struct process *proc);
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
/*
 * Creates a child job and returns its pid in pid_out. This does not wait for
 * foreground completion; callers that want foreground behavior must call
 * job_foreground_pid(pid) and then process_wait_pid(pid).
 */
int process_spawn_from_user(struct vfs *vfs,
                            struct process *proc,
                            char *command_line,
                            const char *const *envp,
                            uint32_t syscall_mode,
                            uint32_t flags,
                            uint32_t *pid_out);
