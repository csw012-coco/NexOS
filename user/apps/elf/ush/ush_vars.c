#include "user/apps/elf/ush/ush_vars_internal.h"

struct ush_alias_entry g_ush_aliases[USH_VAR_MAX];
struct ush_function_entry g_ush_functions[USH_FUNCTION_MAX];
struct ush_script_args_state g_ush_script_args;

void ush_sync_pwd_var_local(const char *cwd) {
    (void)ush_var_assign_local("PWD", cwd != NULL ? cwd : "/", 1);
}

void ush_refresh_cwd_local(char *cwd, uint32_t cwd_size) {
    if (cwd == NULL || cwd_size == 0) {
        return;
    }
    if (getcwd(cwd, cwd_size) < 0 || cwd[0] == '\0') {
        ush_vars_copy_line(cwd, "/", cwd_size);
    }
    ush_sync_pwd_var_local(cwd);
}

void ush_init_vars_local(const char *cwd) {
    uint32_t i = 0;

    while (i < USH_VAR_MAX) {
        g_ush_vars[i].used = 0u;
        g_ush_vars[i].name[0] = '\0';
        g_ush_vars[i].value[0] = '\0';
        g_ush_aliases[i].used = 0u;
        g_ush_aliases[i].name[0] = '\0';
        g_ush_aliases[i].value[0] = '\0';
        i++;
    }
    if (getenv("PATH") == NULL) {
        (void)setenv("PATH", "/cmd", 1);
    }
    if (getenv("SHELL") == NULL) {
        (void)setenv("SHELL", "/cmd/ush", 1);
    }
    if (ush_var_lookup_local("PS") == NULL) {
        (void)ush_var_assign_local("PS", "[%u@%w]> ", 0);
    }
    ush_clear_script_args_local();
    ush_sync_pwd_var_local(cwd);
}
