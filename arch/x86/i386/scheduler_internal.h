#pragma once

#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/proc/runqueue.h"
#include "scheduler.h"

struct syscall_dirent;
struct syscall_fd_info;
struct vfs;

enum {
    I386_FPU_STATE_SIZE = 108
};

struct i386_fpu_state {
    uint8_t bytes[I386_FPU_STATE_SIZE];
};

enum {
    I386_SCHEDULER_TASKS = 8,
    I386_TASK_NAME_SIZE = 16,
    I386_USER_HEAP_BASE = 0x50000000u,
    I386_USER_HEAP_LIMIT = 0x70000000u
};

struct scheduler_task {
    uint32_t ticks;
    uint32_t root;
    uint32_t result;
    uint32_t heap_next;
    uint32_t stack_top;
    uint32_t stack_low;
    uint32_t stack_limit;
    uint32_t stack_grow_events;
    struct address_space address_space;
    struct process process;
    struct user_page_mapping mappings[USER_DYNAMIC_PAGE_LIMIT];
    uint8_t fpu_valid;
    struct i386_fpu_state fpu_state __attribute__((aligned(16)));
};

extern struct scheduler_task tasks[I386_SCHEDULER_TASKS];
extern struct process *task_processes[I386_SCHEDULER_TASKS];
extern struct sched_runqueue runqueue;
extern volatile uint32_t scheduler_active;
extern uint32_t scheduler_tick_count;
extern uint32_t scheduler_kernel_root;
extern uint32_t scheduler_next_pid;
extern uint32_t scheduler_quiet_tty_output;

int i386_scheduler_is_active(void);
uint32_t i386_scheduler_current_slot(void);
struct scheduler_task *i386_scheduler_current_task_mut(void);
struct scheduler_task *i386_scheduler_task_by_pid(uint32_t pid);

void i386_scheduler_fpu_enable(void);
void i386_scheduler_fpu_save(struct i386_fpu_state *state);
void i386_scheduler_fpu_restore(const struct i386_fpu_state *state,
                                uint32_t valid);
void i386_scheduler_backend_save_frame(uint32_t slot);
void i386_scheduler_backend_restore_frame(uint32_t slot);
void i386_scheduler_backend_switch_task(uint32_t previous, uint32_t next);
void i386_scheduler_backend_switch_to_task(uint32_t slot);
void i386_scheduler_backend_switch_to_kernel(void);
int i386_scheduler_backend_enter_task(uint32_t slot);
void i386_scheduler_backend_init_user_context(struct scheduler_task *task,
                                              uint32_t entry,
                                              uint32_t stack,
                                              uint32_t id);
uint32_t i386_scheduler_backend_task_result(
    const struct process_context *context);

int i386_scheduler_run_loaded(const struct process_loaded_image *image,
                              struct process_snapshot *snapshot);
int i386_scheduler_run_loaded_quiet(const struct process_loaded_image *image,
                                    struct process_snapshot *snapshot);
int32_t i386_scheduler_reap_exited_pid(uint32_t pid);
void i386_scheduler_set_console_handle(void *handle);
int32_t i386_scheduler_spawn(uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name);
int32_t i386_scheduler_fork(const struct process_context *context,
                            uint32_t *child_pid_out);
uintptr_t i386_scheduler_exec(const struct process_context *context,
                              uint32_t entry,
                              uint32_t stack,
                              uint32_t root,
                              const char *name);
uintptr_t i386_scheduler_wait(const struct process_context *context,
                              uint32_t pid,
                              int32_t *status,
                              int *blocked,
                              uint32_t user_info,
                              struct process_snapshot *snapshot);
uintptr_t i386_scheduler_exit(const struct process_context *context,
                              int exit_code);
uintptr_t i386_scheduler_yield(const struct process_context *context);
uintptr_t i386_scheduler_sleep(const struct process_context *context,
                               uint32_t ticks);
uint32_t i386_scheduler_page_alloc(void);
uint32_t i386_scheduler_page_alloc_with_prot(int writable);
uint32_t i386_scheduler_page_alloc_at(uint32_t user_page, int writable);
int32_t i386_scheduler_page_protect(uint32_t user_page, int writable);
int32_t i386_scheduler_page_free(uint32_t user_page);
int32_t i386_scheduler_page_free_pid(uint32_t pid, uint32_t user_page);
uint32_t i386_scheduler_shared_page_alloc(void);
int32_t i386_scheduler_shared_page_free(uint32_t frame);
uint32_t i386_scheduler_shared_page_map(uint32_t frame);
int32_t i386_scheduler_shared_page_unmap(uint32_t user_page);
int32_t i386_scheduler_shared_page_unmap_pid(uint32_t pid, uint32_t user_page);
uint32_t i386_scheduler_shared_phys_alloc(void);
int32_t i386_scheduler_shared_phys_free(uint32_t frame);
uint32_t i386_scheduler_shared_phys_map(uint32_t frame);
int32_t i386_scheduler_shared_phys_unmap(uint32_t user_page);
int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags);
int32_t i386_scheduler_opendir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_readdir(struct vfs *vfs,
                               uint32_t fd,
                               struct syscall_dirent *entry);
int32_t i386_scheduler_chdir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_getcwd(char *buffer, uint32_t size);
int32_t i386_scheduler_mkdir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_rmdir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_remove(struct vfs *vfs, const char *path);
int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags);
int32_t i386_scheduler_write(struct vfs *vfs,
                             uint32_t fd,
                             const void *buffer,
                             uint32_t size);
int32_t i386_scheduler_seek(uint32_t fd,
                            int32_t offset,
                            uint32_t whence);
int32_t i386_scheduler_pipe(uint32_t pair[2]);
int32_t i386_scheduler_dup2(uint32_t src_fd, uint32_t dst_fd);
uint32_t i386_scheduler_fd_kind(uint32_t fd);
int32_t i386_scheduler_fd_query(uint32_t fd, struct syscall_fd_info *info);
int32_t i386_scheduler_close(uint32_t fd);
void i386_scheduler_task_init(struct scheduler_task *task,
                              uint32_t entry,
                              uint32_t stack,
                              uint32_t id,
                              uint32_t root,
                              const char *name,
                              uint32_t parent_pid);
void i386_scheduler_task_release(uint32_t slot, int cleanup_pid);
void i386_scheduler_task_reap(uint32_t slot);
