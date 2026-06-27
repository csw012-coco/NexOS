#pragma once

#include <stdint.h>

struct process_context;
struct process_snapshot;
struct syscall_dirent;
struct vfs;

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1);
int i386_scheduler_run_one(uint32_t entry,
                           uint32_t stack,
                           uint32_t root,
                           const char *name);
int32_t i386_scheduler_spawn(uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name);
uintptr_t i386_scheduler_exec(const struct process_context *context,
                             uint32_t entry,
                             uint32_t stack,
                             uint32_t root,
                             const char *name);
uintptr_t i386_scheduler_wait(const struct process_context *context,
                             uint32_t pid,
                             int32_t *status,
                             int *blocked);
const struct process_context *i386_scheduler_tick(
    const struct process_context *context);
uintptr_t i386_scheduler_exit(const struct process_context *context,
                              int exit_code);
uintptr_t i386_scheduler_yield(const struct process_context *context);
uint32_t i386_scheduler_ticks(void);
uint32_t i386_scheduler_switches(void);
uint32_t i386_scheduler_completed(void);
uint32_t i386_scheduler_task_ticks(uint32_t task);
uint32_t i386_scheduler_task_root(uint32_t task);
uint32_t i386_scheduler_task_result(uint32_t task);
uint32_t i386_scheduler_current_pid(void);
uint32_t i386_scheduler_page_alloc(void);
int32_t i386_scheduler_page_free(uint32_t user_page);
int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags);
int32_t i386_scheduler_opendir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_readdir(struct vfs *vfs,
                               uint32_t fd,
                               struct syscall_dirent *entry);
int32_t i386_scheduler_chdir(struct vfs *vfs, const char *path);
int32_t i386_scheduler_getcwd(char *buffer, uint32_t size);
int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags);
int32_t i386_scheduler_write(struct vfs *vfs,
                             uint32_t fd,
                             const void *buffer,
                             uint32_t size);
int32_t i386_scheduler_pipe(uint32_t pair[2]);
int32_t i386_scheduler_dup2(uint32_t src_fd, uint32_t dst_fd);
uint32_t i386_scheduler_fd_kind(uint32_t fd);
int32_t i386_scheduler_close(uint32_t fd);
int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot);
