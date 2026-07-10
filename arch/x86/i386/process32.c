#include "process32.h"

#include "abi/syscall_abi.h"
#include "fs/early_vfs.h"
#include "kernel/internal/proc/process_program_registry_internal.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"
#include "paging.h"
#include "scheduler.h"
#include "user.h"

struct process32_exec_plan {
    char command[NOS_TTY_LINE_BUFFER_SIZE];
    const char *argv[I386_USER_ARG_MAX + 1];
    const char *envp[I386_USER_ENV_MAX + 1];
    int argc;
    char load_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];
};

static struct early_vfs *g_process32_vfs;

void process32_init(struct early_vfs *vfs) {
    g_process32_vfs = vfs;
}

static int process32_parse_command(char *command,
                                   const char *argv[],
                                   int *argc_out) {
    int argc = 0;
    char *cursor = command;

    while (*cursor != '\0' && argc < I386_USER_ARG_MAX) {
        char *out;
        int single_quote = 0;
        int double_quote = 0;

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        out = cursor;
        argv[argc++] = out;
        while (*cursor != '\0') {
            char ch = *cursor;

            if (!single_quote && ch == '\\') {
                cursor++;
                if (*cursor == '\0') {
                    break;
                }
                *out++ = *cursor++;
                continue;
            }
            if (!double_quote && ch == '\'') {
                single_quote = !single_quote;
                cursor++;
                continue;
            }
            if (!single_quote && ch == '"') {
                double_quote = !double_quote;
                cursor++;
                continue;
            }
            if (!single_quote && !double_quote &&
                (ch == ' ' || ch == '\t')) {
                cursor++;
                break;
            }
            *out++ = *cursor++;
        }
        *out = '\0';
    }
    *argc_out = argc;
    return argc != 0;
}

static int process32_spawn_mode_valid(uint32_t mode, uint32_t flags) {
    return (mode == SYS_SPAWN_AUTO || mode == SYS_SPAWN_ELF) &&
           (flags & ~SYS_SPAWN_BACKGROUND) == 0u;
}

static void process32_copy_text(char *dst,
                                uint32_t dst_size,
                                const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int process32_text_equal_ignore_case(const char *a, const char *b) {
    uint32_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    for (;;) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - ('a' - 'A'));
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
        if (ca == '\0') {
            return 1;
        }
        i++;
    }
}

static int process32_image_name_has_path(const char *name) {
    uint32_t i = 0u;

    while (name != 0 && name[i] != '\0') {
        if (name[i] == '/' || name[i] == '.') {
            return name[i] == '/';
        }
        i++;
    }
    return 0;
}

static void process32_resolve_load_path(const char *image_name,
                                        char *out,
                                        uint32_t out_size) {
    if (process32_text_equal_ignore_case(image_name, "NEXBOX.ELF")) {
        process32_copy_text(out, out_size, "/BOOT/NEXBOX32.ELF");
        return;
    }
    if (process32_image_name_has_path(image_name)) {
        process32_copy_text(out, out_size, image_name);
        return;
    }
    process32_copy_text(out, out_size, "/BOOT/");
    if (out_size > 6u) {
        uint32_t len = 6u;
        uint32_t i = 0u;

        while (image_name != 0 && image_name[i] != '\0' && len + 1u < out_size) {
            out[len++] = image_name[i++];
        }
        out[len] = '\0';
    }
}

static int process32_exec_plan_build(const char *command,
                                     uint32_t mode,
                                     struct process32_exec_plan *plan) {
    static const char *const default_envp[] = {
        "ARCH=i386",
        "OS=NexOS",
        0
    };
    uint32_t length = 0u;

    if (command == 0 || plan == 0) {
        return 0;
    }
    while (command[length] != '\0' && length + 1u < sizeof(plan->command)) {
        plan->command[length] = command[length];
        length++;
    }
    plan->command[length] = '\0';
    if (!i386_scheduler_resolve_exec_command_line(plan->command,
                                                  sizeof(plan->command))) {
        return 0;
    }
    if (!process32_parse_command(plan->command, plan->argv, &plan->argc)) {
        return 0;
    }
    plan->argv[plan->argc] = 0;
    for (uint32_t i = 0u; i < I386_USER_ENV_MAX + 1u; i++) {
        plan->envp[i] = 0;
    }
    for (uint32_t i = 0u;
         i < I386_USER_ENV_MAX && default_envp[i] != 0;
         i++) {
        plan->envp[i] = default_envp[i];
    }
    if (mode == SYS_SPAWN_AUTO) {
        const struct process_program *program =
            process_find_program_internal(plan->argv[0]);

        process32_resolve_load_path(program != 0 ? program->image_name : plan->argv[0],
                                    plan->load_path,
                                    sizeof(plan->load_path));
    } else {
        process32_copy_text(plan->load_path,
                            sizeof(plan->load_path),
                            plan->argv[0]);
    }
    process32_copy_text(plan->name, sizeof(plan->name), plan->load_path);
    return 1;
}

static int process32_load_exec_plan(const struct process32_exec_plan *plan,
                                    struct i386_user_image *image) {
    if (g_process32_vfs == 0 ||
        plan == 0 || image == 0 || plan->argc <= 0 || plan->argv[0] == 0) {
        return 0;
    }
    return i386_user_load_elf_space_args(g_process32_vfs,
                                         plan->load_path,
                                         0xbfffe000u,
                                         plan->argc,
                                         plan->argv,
                                         plan->envp,
                                         image);
}

static int process32_build_and_load(const char *command,
                                    uint32_t mode,
                                    struct process32_exec_plan *plan,
                                    struct i386_user_image *image) {
    return process32_exec_plan_build(command, mode, plan) &&
           process32_load_exec_plan(plan, image);
}

int32_t process32_spawn_from_user(const char *command,
                                  uint32_t mode,
                                  uint32_t flags) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    uint32_t current_root = i386_paging_root();

    if (!process32_spawn_mode_valid(mode, flags)) {
        return -1;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!process32_build_and_load(command, mode, &plan, &image)) {
        i386_paging_switch(current_root);
        return -1;
    }
    i386_paging_switch(current_root);
    return i386_scheduler_spawn(image.entry,
                                image.stack_top,
                                image.root,
                                plan.name);
}

uintptr_t process32_exec_replace_from_user(
    const struct process_context *context,
    const char *command) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    uint32_t current_root = i386_paging_root();

    if (context == 0 || command == 0) {
        return 0u;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!process32_build_and_load(command, SYS_SPAWN_AUTO, &plan, &image)) {
        i386_paging_switch(current_root);
        return 0u;
    }
    i386_paging_switch(current_root);
    return i386_scheduler_exec(context,
                               image.entry,
                               image.stack_top,
                               image.root,
                               plan.name);
}

int process32_run_command(const char *command,
                          struct process_snapshot *process) {
    struct i386_user_image image;
    struct process32_exec_plan plan;
    uint32_t current_root = i386_paging_root();

    if (process == 0) {
        return 0;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!process32_build_and_load(command, SYS_SPAWN_AUTO, &plan, &image)) {
        i386_paging_switch(current_root);
        return 0;
    }
    i386_paging_switch(current_root);
    if (!i386_scheduler_run_one(image.entry,
                                image.stack_top,
                                image.root,
                                plan.name) ||
        !i386_scheduler_process_snapshot(0u, process)) {
        return 0;
    }
    return process->state == PROCESS_STATE_EXITED;
}
