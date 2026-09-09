#include "user/apps/elf/ush/ush_exec_internal.h"

static const char ush_external_ansi_error[] = "\x1b[1;31m";
static const char ush_external_ansi_value[] = "\x1b[1;33m";

static int ush_external_should_report_exec_rc(int rc) {
    return rc == -NEX_ERR_ACCES ||
           rc == -NEX_ERR_PERM ||
           rc == -NEX_ERR_NOENT ||
           rc == -NEX_ERR_INVAL;
}

static void ush_report_exec_candidate_failure(const char *command, int rc) {
    if (!ush_external_should_report_exec_rc(rc)) {
        return;
    }
    ush_write_colored_err(ush_external_ansi_error, "exec failed: ");
    ush_write_colored_err(ush_external_ansi_value, command != NULL ? command : "");
    write_err_str(" rc=");
    eprintf("%d\n", rc);
}

static void ush_report_exec_direct_failure(const char *command, int rc) {
    ush_write_colored_err(ush_external_ansi_error, "exec failed: ");
    ush_write_colored_err(ush_external_ansi_value, command != NULL ? command : "");
    write_err_str(" rc=");
    eprintf("%d\n", rc);
}

static int ush_build_nexbox_applet_command(const char *line,
                                           const char *token,
                                           const char *after_token,
                                           char *out,
                                           uint32_t out_size) {
    const char *rest;

    if (line == NULL || token == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    if (streq_local(token, "nexbox") || streq_local(token, "nexbox32")) {
        copy_line_local(out, "/cmd/nexbox", out_size);
        rest = skip_spaces_local(after_token);
        if (rest != NULL && rest[0] != '\0') {
            return ush_build_prefixed_command("/cmd/nexbox", rest, out, out_size);
        }
        return out[0] != '\0';
    }
    return ush_build_prefixed_command("/cmd/nexbox", line, out, out_size);
}

static int ush_try_search_shebang_command(const char *line,
                                          const char *search_command,
                                          int background) {
    char path[64];
    const char *cursor = search_command;

    if (!read_token_local(&cursor, path, sizeof(path))) {
        return 0;
    }
    return ush_try_shebang_command(line, path, background);
}

int ush_try_external_command(char *cwd, const char *line, int background, int *handled_out) {
    char token[64];
    char command[256];
    char action_command[256];
    char search_command[256];
    const char *cursor = line;
    int explicit_path;
    int rc;

    if (!read_token_local(&cursor, token, sizeof(token))) {
        if (handled_out != NULL) {
            *handled_out = 0;
        }
        return 0;
    }
    cursor = skip_spaces_local(cursor);
    if (contains_char_local(token, '.') &&
        token[0] != '.' &&
        !contains_char_local(token, '/') &&
        ush_build_action_command(line, action_command, sizeof(action_command))) {
        rc = ush_spawn_command_local(action_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    explicit_path = ush_program_name_needs_path(token, 1);
    if (explicit_path) {
        if (!ush_build_program_command(line, token, 1, command, sizeof(command))) {
            return 1;
        }
        rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        if (ends_with_ignore_case_local(token, ".sh")) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return ush_try_sh_script_command(cwd, line, token, background);
        }
        if (ush_try_shebang_command(line, token, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
        if (ush_try_wav_command(line, token, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
        ush_write_colored_err(ush_external_ansi_error, "exec failed: ");
        ush_write_colored_err(ush_external_ansi_value, command);
        write_err_str(" rc=");
        eprintf("%d\n", rc);
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 1;
    }

    (void)cwd;
    if (streq_local(token, "sudo")) {
        if (!ush_build_prefixed_command("/cmd/sudo", cursor, command, sizeof(command))) {
            return 1;
        }
        rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        ush_report_exec_direct_failure(command, rc);
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 1;
    }
    if (ush_is_nexbox32_applet_name(token) &&
        ush_build_nexbox_applet_command(line,
                                        token,
                                        cursor,
                                        search_command,
                                        sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        ush_report_exec_direct_failure(search_command, rc);
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 1;
    }

    rc = ush_spawn_command_local(line, SYS_SPAWN_AUTO, background);
    if (rc == 0) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return background ? 0 : ush_last_foreground_status_local();
    }

    if (ush_build_cmd_search_command_lower(line, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        ush_report_exec_candidate_failure(search_command, rc);
        if (ush_try_search_shebang_command(line, search_command, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
    }

    if (ush_build_cmd_search_command(line, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        ush_report_exec_candidate_failure(search_command, rc);
        if (ush_try_search_shebang_command(line, search_command, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
    }

    if (ush_try_wav_command(line, token, background)) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 0;
    }

    if (ends_with_ignore_case_local(token, ".sh")) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return ush_try_sh_script_command(cwd, line, token, background);
    }

    if (handled_out != NULL) {
        *handled_out = 0;
    }
    return 0;
}
