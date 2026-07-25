#include "user/apps/elf/ush_exec_internal.h"

static const char ush_dispatch_ansi_error[] = "\x1b[1;31m";
static const char ush_dispatch_ansi_value[] = "\x1b[1;33m";

int ush_execute_command_core(char *cwd, const char *line, int background) {
    char name[USH_VAR_NAME_MAX + 1];
    char value[USH_VAR_VALUE_MAX + 1];
    uint64_t exit_code = 0;
    int handled = 0;

    if (background && (streq_local(line, "exit") ||
                       starts_with_local(line, "exit ") ||
                       streq_local(line, "cd") ||
                       streq_local(line, "cd..") ||
                       starts_with_local(line, "cd ") ||
                       streq_local(line, "set") ||
                       starts_with_local(line, "set ") ||
                       streq_local(line, "export") ||
                       starts_with_local(line, "export ") ||
                       streq_local(line, "alias") ||
                       starts_with_local(line, "alias ") ||
                       streq_local(line, "functions") ||
                       streq_local(line, "history") ||
                       streq_local(line, "source") ||
                       starts_with_local(line, "source ") ||
                       streq_local(line, ".") ||
                       starts_with_local(line, ". ") ||
                       streq_local(line, "session load") ||
                       starts_with_local(line, "session load ") ||
                       streq_local(line, "preload") ||
                       starts_with_local(line, "preload ") ||
                       streq_local(line, "exec") ||
                       starts_with_local(line, "exec "))) {
        write_err_str("background: shell builtin cannot run in background\n");
        return 1;
    }
    if (streq_local(line, "exit")) {
        exit_with_code(0);
    }
    if (starts_with_local(line, "exit ")) {
        if (!ush_parse_exit_code_local(line + 4, &exit_code)) {
            ush_write_error("usage: exit [code]\n");
            return 1;
        }
        exit_with_code(exit_code);
    }
    if (streq_local(line, "cd")) {
        return ush_change_directory(cwd, 64u, "/");
    }
    if (streq_local(line, "cd..")) {
        return ush_change_directory(cwd, 64u, "..");
    }
    if (starts_with_local(line, "cd ")) {
        return ush_change_directory(cwd, 64u, line + 3);
    }
    if (streq_local(line, "set")) {
        ush_var_list_shell_local();
        return 0;
    }
    if (streq_local(line, "alias")) {
        ush_alias_list_local();
        return 0;
    }
    if (streq_local(line, "functions")) {
        ush_function_list_local();
        return 0;
    }
    if (streq_local(line, "history")) {
        ush_history_list();
        return 0;
    }
    if (streq_local(line, "source") || streq_local(line, ".")) {
        write_err_str("usage: source <file> [args]\n");
        return 1;
    }
    if (starts_with_local(line, "source ")) {
        return ush_source_script_local(cwd, line + 7);
    }
    if (starts_with_local(line, ". ")) {
        return ush_source_script_local(cwd, line + 2);
    }
    if (streq_local(line, "session load")) {
        write_err_str("usage: session load <name>\n");
        return 1;
    }
    if (starts_with_local(line, "session load ")) {
        return ush_session_load_local(cwd, line + 13);
    }
    if (streq_local(line, "preload")) {
        write_err_str("usage: preload <file>\n");
        return 1;
    }
    if (starts_with_local(line, "preload ")) {
        return ush_preload_file_local(skip_spaces_local(line + 8));
    }
    if (starts_with_local(line, "set ")) {
        if (!ush_parse_assignment_local(line + 4, name, sizeof(name), value, sizeof(value))) {
            ush_write_error("usage: set NAME=value\n");
            return 1;
        }
        if (!ush_var_assign_local(name, value, 0)) {
            ush_write_error("set: could not store variable\n");
            return 1;
        }
        return 0;
    }
    if (streq_local(line, "export")) {
        ush_var_list_local(1);
        return 0;
    }
    if (starts_with_local(line, "alias ")) {
        if (!ush_parse_assignment_local(line + 6, name, sizeof(name), value, sizeof(value))) {
            ush_write_error("usage: alias NAME=value\n");
            return 1;
        }
        if (!ush_alias_assign_local(name, value)) {
            ush_write_error("alias: could not store alias\n");
            return 1;
        }
        return 0;
    }
    if (starts_with_local(line, "export ")) {
        char export_arg[USH_LINE_MAX + 1];

        copy_line_local(export_arg, line + 7, sizeof(export_arg));
        trim_in_place_local(export_arg);
        if (export_arg[0] == '\0') {
            ush_write_error("usage: export NAME or export NAME=value\n");
            return 1;
        }
        if (contains_char_local(export_arg, '=')) {
            if (!ush_parse_assignment_local(export_arg, name, sizeof(name), value, sizeof(value))) {
                ush_write_error("usage: export NAME or export NAME=value\n");
                return 1;
            }
            if (!ush_var_export_local(name, value)) {
                ush_write_error("export: could not store variable\n");
                return 1;
            }
            return 0;
        }
        if (!ush_var_name_valid_local(export_arg) || !ush_var_export_local(export_arg, NULL)) {
            ush_write_error("export: invalid variable name\n");
            return 1;
        }
        return 0;
    }
    if (streq_local(line, "exec")) {
        ush_write_error("usage: exec <command> [args]\n");
        return 1;
    }
    if (starts_with_local(line, "exec ")) {
        int rc = exec_replace(line + 5);

        if (rc != 0) {
            ush_write_colored_err(ush_dispatch_ansi_error, "exec failed: ");
            ush_write_colored_err(ush_dispatch_ansi_value, line + 5);
            write_err_str(" rc=");
            eprintf("%d\n", rc);
            return 1;
        }
        return 0;
    }
    {
        int function_handled = 0;
        int function_rc;

        if (background) {
            char token[64];
            const char *cursor = line;

            if (read_token_local(&cursor, token, sizeof(token)) &&
                ush_function_lookup_local(token) != NULL) {
                write_err_str("background: shell function cannot run in background\n");
                return 1;
            }
        }
        function_rc = ush_try_function_call_local(cwd, line, 0, &function_handled);
        if (function_handled) {
            return function_rc;
        }
    }
    {
        int status;

        handled = 0;
        status = ush_try_external_command(cwd, line, background, &handled);
        if (handled) {
            if (status == 0 && starts_with_local(line, "switch_root ")) {
                (void)chdir("/");
            }
            if (status == 0) {
                ush_refresh_cwd_local(cwd, 64u);
            }
            return status;
        }
    }
    ush_write_colored_err(ush_dispatch_ansi_error, "unknown command: ");
    ush_write_colored_err(ush_dispatch_ansi_value, line);
    write_err_str("\n");
    return 1;
}

int ush_execute_with_redirection(char *cwd, const struct ush_command_spec *spec, int background) {
    uint32_t saved[3];
    int rc;

    if (!ush_save_stdio(saved,
                        spec->input[0] != '\0',
                        spec->output[0] != '\0',
                        spec->stderr_to_stdout || spec->err_output[0] != '\0')) {
        write_err_str("redirect: stdio save failed\n");
        return 1;
    }

    if (!ush_apply_redirections(cwd, spec)) {
        ush_restore_stdio(saved);
        return 1;
    }

    rc = ush_execute_command_core(cwd, spec->command, background);

    ush_restore_stdio(saved);

    return rc;
}

