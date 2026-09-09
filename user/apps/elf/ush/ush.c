#include "user/apps/elf/ush/ush_shared.h"

static int ush_handle_function_definition(struct ush_editor *editor,
                                          char *line,
                                          char *function_name,
                                          char *function_body) {
    int complete = 0;
    int def_rc = ush_parse_function_start_local(line,
                                                function_name,
                                                USH_VAR_NAME_MAX + 1u,
                                                function_body,
                                                USH_FUNCTION_BODY_MAX + 1u,
                                                &complete);

    if (def_rc < 0) {
        write_err_str("function: parse failed\n");
        return 1;
    }
    if (def_rc == 0) {
        return 0;
    }
    while (!complete) {
        ush_prompt_override("func> ");
        write_str("func> ");
        if (!read_line_chars(editor, line, USH_LINE_MAX + 1u)) {
            ush_prompt_override(NULL);
            ush_write_error("read failed\n");
            exit_with_code(1);
        }
        ush_prompt_override(NULL);
        if (ush_function_close_line_local(line)) {
            complete = 1;
            break;
        }
        if (!ush_function_body_append_local(function_body,
                                            USH_FUNCTION_BODY_MAX + 1u,
                                            line)) {
            write_err_str("function: body too long\n");
            break;
        }
    }
    if (complete) {
        (void)ush_store_function_local(function_name, function_body);
    }
    return 1;
}

int main(int argc, char **argv) {
    struct ush_editor editor = {0};
    char invoked_line[64];
    char line[64];
    char cwd[64];
    char function_name[USH_VAR_NAME_MAX + 1];
    char function_body[USH_FUNCTION_BODY_MAX + 1];
    const char *init_path = NULL;

    ush_refresh_cwd_local(cwd, sizeof(cwd));
    ush_init_vars_local(cwd);
    ush_load_config_local();

    if (argc > 2 && ush_is_direct_shell_invocation(argv[0]) && strcmp(argv[1], "--tty") == 0) {
        if (!ush_bind_interactive_stdio_path(argv[2])) {
            ush_write_error("ush: tty open failed\n");
            exit_with_code(1);
        }

        if (argc > 4 && strcmp(argv[3], "--init") == 0) {
            init_path = argv[4];
        } else if (argc > 3) {
            ush_write_error("usage: ush --tty <path> [--init <script>]\n");
            exit_with_code(1);
        }
    } else if (argc > 1 && ush_is_direct_shell_invocation(argv[0])) {
        return ush_run_script_file(cwd, argv[1], argc - 1, argv + 1);
    } else if (ush_build_invoked_command_line(argc, argv, invoked_line, sizeof(invoked_line))) {
        (void)ush_execute_line(cwd, invoked_line);
        return 0;
    } else {
        ush_bind_interactive_stdio();
    }

    if (init_path != NULL) {
        char *init_argv[1];

        init_argv[0] = (char *)init_path;
        (void)ush_run_script_file(cwd, init_path, 1, init_argv);
        ush_refresh_cwd_local(cwd, sizeof(cwd));
    }

    for (;;) {
        ush_prompt_sync(cwd);
        ush_write_prompt();
        if (!read_line_chars(&editor, line, sizeof(line))) {
            ush_write_error("read failed\n");
            exit_with_code(1);
        }
        if (line[0] == '\0') {
            continue;
        }
        if (ush_handle_function_definition(&editor, line, function_name, function_body)) {
            continue;
        }
        if (ush_execute_line(cwd, line) < 0) {
            return 0;
        }
    }
}
