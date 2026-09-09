#include "user/apps/elf/ush/ush_vars_internal.h"

const char *ush_special_var_lookup_local(const char *name) {
    uint32_t index = 0;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    if (name[1] == '\0') {
        if (name[0] == '#') {
            return g_ush_script_args.count;
        }
        if (name[0] == '@' || name[0] == '*') {
            return g_ush_script_args.joined;
        }
        if (name[0] >= '0' && name[0] <= '9') {
            index = (uint32_t)(name[0] - '0');
            return index < 10u ? g_ush_script_args.argv[index] : NULL;
        }
    }
    return NULL;
}

void ush_set_script_args_local(int argc, char **argv) {
    uint32_t i;
    uint32_t joined_len = 0;
    uint32_t count = 0;

    ush_clear_script_args_local();
    if (argc <= 0 || argv == NULL) {
        return;
    }
    for (i = 0; i < 10u && i < (uint32_t)argc; i++) {
        ush_vars_copy_line(g_ush_script_args.argv[i],
                           argv[i] != NULL ? argv[i] : "",
                           sizeof(g_ush_script_args.argv[i]));
    }
    g_ush_script_args.argc = (uint32_t)argc;
    count = (uint32_t)argc > 0u ? (uint32_t)argc - 1u : 0u;
    (void)snprintf(g_ush_script_args.count, sizeof(g_ush_script_args.count), "%u", count);

    for (i = 1u; i < (uint32_t)argc; i++) {
        const char *arg = argv[i] != NULL ? argv[i] : "";
        uint32_t arg_len = ush_vars_strlen(arg);
        uint32_t j;

        if (arg_len == 0) {
            continue;
        }
        if (joined_len != 0u) {
            if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
                break;
            }
            g_ush_script_args.joined[joined_len++] = ' ';
        }
        for (j = 0; j < arg_len; j++) {
            if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
                break;
            }
            g_ush_script_args.joined[joined_len++] = arg[j];
        }
        if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
            break;
        }
    }
    g_ush_script_args.joined[joined_len] = '\0';
}

void ush_clear_script_args_local(void) {
    uint32_t i;

    g_ush_script_args.argc = 0u;
    g_ush_script_args.joined[0] = '\0';
    ush_vars_copy_line(g_ush_script_args.count, "0", sizeof(g_ush_script_args.count));
    for (i = 0; i < 10u; i++) {
        g_ush_script_args.argv[i][0] = '\0';
    }
}

void ush_save_script_args_local(struct ush_script_args_snapshot *out) {
    uint32_t i;

    if (out == NULL) {
        return;
    }
    out->argc = g_ush_script_args.argc;
    ush_vars_copy_line(out->joined, g_ush_script_args.joined, sizeof(out->joined));
    ush_vars_copy_line(out->count, g_ush_script_args.count, sizeof(out->count));
    for (i = 0; i < 10u; i++) {
        ush_vars_copy_line(out->argv[i], g_ush_script_args.argv[i], sizeof(out->argv[i]));
    }
}

void ush_restore_script_args_local(const struct ush_script_args_snapshot *snapshot) {
    uint32_t i;

    if (snapshot == NULL) {
        return;
    }
    g_ush_script_args.argc = snapshot->argc;
    ush_vars_copy_line(g_ush_script_args.joined, snapshot->joined, sizeof(g_ush_script_args.joined));
    ush_vars_copy_line(g_ush_script_args.count, snapshot->count, sizeof(g_ush_script_args.count));
    for (i = 0; i < 10u; i++) {
        ush_vars_copy_line(g_ush_script_args.argv[i], snapshot->argv[i], sizeof(g_ush_script_args.argv[i]));
    }
}
