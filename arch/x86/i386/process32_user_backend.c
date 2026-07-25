#include "process32.h"
#include "process32_internal.h"

#include "abi/syscall_abi.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "paging.h"

static int process32_prepare_loaded_command(const char *command,
                                            uint32_t mode,
                                            struct process32_exec_plan *plan,
                                            struct i386_user_image *image,
                                            struct process_loaded_image *loaded) {
    uint32_t current_root;

    if (plan == 0 || image == 0 || loaded == 0) {
        return 0;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    if (!process32_build_and_load(command, mode, plan, image)) {
        i386_paging_switch(current_root);
        return 0;
    }
    i386_paging_switch(current_root);
    process32_fill_loaded_image(image, plan, loaded);
    return 1;
}

void process32_fill_loaded_image(const struct i386_user_image *image,
                                 const struct process32_exec_plan *plan,
                                 struct process_loaded_image *out) {
    if (out == 0) {
        return;
    }
    out->entry = image != 0 ? image->entry : 0u;
    out->stack = image != 0 ? image->stack_top : 0u;
    out->root = image != 0 ? image->root : 0u;
    out->name = plan != 0 ? plan->name : 0;
}

int32_t process32_spawn_from_user(const char *command,
                                  uint32_t mode,
                                  uint32_t flags) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    struct process_loaded_image loaded;

    if (!process32_spawn_mode_valid(mode, flags)) {
        return -1;
    }
    if (!process32_prepare_loaded_command(command,
                                          mode,
                                          &plan,
                                          &image,
                                          &loaded)) {
        return -1;
    }
    return process_scheduler_spawn_loaded(&loaded);
}

int32_t process32_fork_from_user(const struct process_context *context,
                                 uint32_t *child_pid_out) {
    return process_scheduler_fork_current(context, child_pid_out);
}

uintptr_t process32_exec_replace_from_user(
    const struct process_context *context,
    const char *command) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    struct process_loaded_image loaded;
    uintptr_t action;

    if (context == 0 || command == 0) {
        return 0u;
    }
    if (!process32_prepare_loaded_command(command,
                                          SYS_SPAWN_AUTO,
                                          &plan,
                                          &image,
                                          &loaded)) {
        return 0u;
    }
    action = process_scheduler_exec_replace_loaded(context, &loaded);
    if (action == 0u) {
        process32_destroy_loaded_image(&image);
    }
    return action;
}

int process32_run_command(const char *command,
                          struct process_snapshot *process) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    struct process_loaded_image loaded;

    if (process == 0) {
        return 0;
    }
    if (!process32_prepare_loaded_command(command,
                                          SYS_SPAWN_AUTO,
                                          &plan,
                                          &image,
                                          &loaded)) {
        return 0;
    }
    return process_scheduler_run_loaded(&loaded, process);
}
