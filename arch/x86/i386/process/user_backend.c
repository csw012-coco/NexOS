#include "process.h"
#include "internal.h"

#include "abi/syscall_abi.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "../mm/paging.h"

static int32_t process32_errno_from_exec_error(uint32_t error) {
    switch (error) {
        case PROCESS_EXEC_ERR_BAD_ARGS:
            return -NEX_ERR_INVAL;
        case PROCESS_EXEC_ERR_FILE_NOT_FOUND:
            return -NEX_ERR_NOENT;
        case PROCESS_EXEC_ERR_FILE_TOO_LARGE:
            return -NEX_ERR_FBIG;
        case PROCESS_EXEC_ERR_FILE_READ:
            return -NEX_ERR_IO;
        case PROCESS_EXEC_ERR_ELF_HEADER:
            return -NEX_ERR_NOEXEC;
        case PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS:
        case PROCESS_EXEC_ERR_ELF_SEGMENT_ADDR:
            return -NEX_ERR_BAD_ELF;
        case PROCESS_EXEC_ERR_ELF_SEGMENT_MAP:
        case PROCESS_EXEC_ERR_STACK_ALLOC:
            return -NEX_ERR_NOMEM;
        case PROCESS_EXEC_ERR_ENTER:
            return -NEX_ERR_EXEC;
        case PROCESS_EXEC_OK:
        default:
            return -NEX_ERR_EXEC;
    }
}

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
    out->caps = plan != 0 ? plan->caps : 0u;
    out->uid = plan != 0 ? plan->uid : 0u;
    out->gid = plan != 0 ? plan->gid : 0u;
    out->caps_set = plan != 0 ? plan->caps_set : 0u;
    out->identity_set = plan != 0 ? plan->identity_set : 0u;
}

int32_t process32_spawn_from_user(const char *command,
                                  uint32_t mode,
                                  uint32_t flags) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    struct process_loaded_image loaded;
    int32_t pid;

    if (!process32_spawn_mode_valid(mode, flags)) {
        return -NEX_ERR_INVAL;
    }
    if (!process32_prepare_loaded_command(command,
                                          mode,
                                          &plan,
                                          &image,
                                          &loaded)) {
        return process32_errno_from_exec_error(g_process_exec_last_error);
    }
    pid = process_scheduler_spawn_loaded(&loaded);
    if (pid <= 0) {
        process32_destroy_loaded_image(&image);
    }
    return pid;
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
