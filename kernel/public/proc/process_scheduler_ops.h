#pragma once

#include <stdint.h>
#include "kernel/public/proc/context.h"

struct process_snapshot;

struct process_loaded_image {
    uint64_t entry;
    uint64_t stack;
    uint64_t root;
    const char *name;
};

struct process_scheduler_ops {
    int32_t (*spawn_loaded)(const struct process_loaded_image *image);
    int (*run_loaded)(const struct process_loaded_image *image,
                      struct process_snapshot *snapshot);
    int (*run_loaded_quiet)(const struct process_loaded_image *image,
                            struct process_snapshot *snapshot);
    int32_t (*fork_current)(const struct process_context *context,
                            uint32_t *child_pid_out);
    uintptr_t (*exec_replace_loaded)(
        const struct process_context *context,
        const struct process_loaded_image *image);
    uintptr_t (*wait)(const struct process_context *context,
                      uint32_t pid,
                      int32_t *status,
                      int *blocked,
                      uint32_t user_info,
                      struct process_snapshot *snapshot);
    uintptr_t (*exit)(const struct process_context *context,
                      int exit_code);
    uintptr_t (*yield)(const struct process_context *context);
    uintptr_t (*sleep)(const struct process_context *context,
                       uint32_t ticks);
    uint32_t (*ticks)(void);
    uint32_t (*current_pid)(void);
    int32_t (*kill)(uint32_t pid);
    int (*snapshot)(uint32_t task, struct process_snapshot *snapshot);
    int32_t (*reap_exited_pid)(uint32_t pid);
};

void process_scheduler_ops_register(const struct process_scheduler_ops *ops);
int32_t process_scheduler_spawn_image(uint32_t entry,
                                      uint32_t stack,
                                      uint32_t root,
                                      const char *name);
int32_t process_scheduler_spawn_loaded(
    const struct process_loaded_image *image);
int process_scheduler_run_loaded(const struct process_loaded_image *image,
                                 struct process_snapshot *snapshot);
int process_scheduler_run_loaded_quiet(const struct process_loaded_image *image,
                                       struct process_snapshot *snapshot);
int32_t process_scheduler_fork_current(const struct process_context *context,
                                       uint32_t *child_pid_out);
uintptr_t process_scheduler_exec_replace(const struct process_context *context,
                                         uint32_t entry,
                                         uint32_t stack,
                                         uint32_t root,
                                         const char *name);
uintptr_t process_scheduler_exec_replace_loaded(
    const struct process_context *context,
    const struct process_loaded_image *image);
uintptr_t process_scheduler_wait(const struct process_context *context,
                                 uint32_t pid,
                                 int32_t *status,
                                 int *blocked,
                                 uint32_t user_info,
                                 struct process_snapshot *snapshot);
uintptr_t process_scheduler_exit(const struct process_context *context,
                                 int exit_code);
uintptr_t process_scheduler_yield(const struct process_context *context);
uintptr_t process_scheduler_sleep(const struct process_context *context,
                                  uint32_t ticks);
uint32_t process_scheduler_ticks(void);
uint32_t process_scheduler_current_pid(void);
int32_t process_scheduler_kill(uint32_t pid);
int process_scheduler_snapshot(uint32_t task,
                               struct process_snapshot *snapshot);
int32_t process_scheduler_reap_exited_pid(uint32_t pid);
