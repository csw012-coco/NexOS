#include "process32_internal.h"

#include "kernel/internal/proc/process_program_registry_internal.h"
#include "kernel/public/proc/process_command.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

int process32_spawn_mode_valid(uint32_t mode, uint32_t flags) {
    if ((flags & ~SYS_SPAWN_BACKGROUND) != 0u) {
        return 0;
    }
    switch (mode) {
        case SYS_SPAWN_AUTO:
        case SYS_SPAWN_ELF:
            return 1;
        default:
            return 0;
    }
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

int process32_exec_plan_build(const char *command,
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
    if (!process_command_resolve_exec_line(process_current(),
                                           plan->command,
                                           sizeof(plan->command))) {
        return 0;
    }
    if (!process_command_parse_argv(plan->command,
                                    plan->argv,
                                    I386_USER_ARG_MAX,
                                    &plan->argc)) {
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

int process32_build_and_load(const char *command,
                             uint32_t mode,
                             struct process32_exec_plan *plan,
                             struct i386_user_image *image) {
    return process32_exec_plan_build(command, mode, plan) &&
           process32_load_exec_plan(plan, image);
}
