#pragma once

#include "abi/syscall_abi.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "user.h"

struct early_vfs;
struct vfs;

struct process32_exec_plan {
    char command[NOS_TTY_LINE_BUFFER_SIZE];
    const char *argv[I386_USER_ARG_MAX + 1];
    const char *envp[I386_USER_ENV_MAX + 1];
    int argc;
    char load_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];
};

extern struct early_vfs *g_process32_vfs;
extern struct vfs *g_process32_runtime_vfs;

int process32_spawn_mode_valid(uint32_t mode, uint32_t flags);
int process32_exec_plan_build(const char *command,
                              uint32_t mode,
                              struct process32_exec_plan *plan);
int process32_load_exec_plan(const struct process32_exec_plan *plan,
                             struct i386_user_image *image);
int process32_build_and_load(const char *command,
                             uint32_t mode,
                             struct process32_exec_plan *plan,
                             struct i386_user_image *image);
void process32_destroy_loaded_image(struct i386_user_image *image);
void process32_fill_loaded_image(const struct i386_user_image *image,
                                 const struct process32_exec_plan *plan,
                                 struct process_loaded_image *out);
int32_t process32_spawn_from_user(const char *command,
                                  uint32_t mode,
                                  uint32_t flags);
int32_t process32_fork_from_user(const struct process_context *context,
                                 uint32_t *child_pid_out);
uintptr_t process32_exec_replace_from_user(
    const struct process_context *context,
    const char *command);
int process32_run_command(const char *command,
                          struct process_snapshot *process);
