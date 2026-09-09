#include "user/apps/elf/ush/ush_exec_internal.h"

static const char ush_script_ansi_error[] = "\x1b[1;31m";
static const char ush_script_ansi_value[] = "\x1b[1;33m";

int ush_source_script_local(char *cwd, const char *text) {
    char arg_storage[10][USH_LINE_MAX + 1];
    char *argv[10];
    const char *cursor = text;
    int argc = 0;

    if (text == NULL) {
        write_err_str("usage: source <file> [args]\n");
        return 1;
    }
    while (argc < 10 && read_token_local(&cursor, arg_storage[argc], sizeof(arg_storage[argc]))) {
        argv[argc] = arg_storage[argc];
        argc++;
    }
    cursor = skip_spaces_local(cursor);
    if (argc == 0 || (cursor != NULL && *cursor != '\0')) {
        write_err_str("usage: source <file> [args]\n");
        return 1;
    }
    return ush_run_script_file(cwd, argv[0], argc, argv);
}

int ush_session_load_local(char *cwd, const char *text) {
    char name[32];
    char path[64];
    char *argv[1];
    const char *cursor = text;

    if (!read_token_local(&cursor, name, sizeof(name))) {
        write_err_str("usage: session load <name>\n");
        return 1;
    }
    for (uint32_t i = 0; name[i] != '\0'; i++) {
        char ch = name[i];

        if (i >= 24u ||
            !((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.')) {
            write_err_str("session: invalid name\n");
            return 1;
        }
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        write_err_str("usage: session load <name>\n");
        return 1;
    }
    if (snprintf(path, sizeof(path), "/system/session/images/%s.ush", name) < 0) {
        write_err_str("session: restore path failed\n");
        return 1;
    }
    argv[0] = path;
    return ush_run_script_file(cwd, path, 1, argv);
}

static int ush_parse_shebang_command(const char *path,
                                     const char *original_line,
                                     char *out,
                                     uint32_t out_size) {
    char header[128];
    char interpreter[64];
    char extra[64];
    const char *cursor;
    uint32_t out_len;
    uint32_t i;
    int fd;

    fd = open(path, 0);
    if (fd < 0) {
        return 0;
    }
    if (read_line((uint32_t)fd, header, sizeof(header)) == 0) {
        close((uint32_t)fd);
        return 0;
    }
    close((uint32_t)fd);
    if (header[0] != '#' || header[1] != '!') {
        return 0;
    }

    cursor = header + 2;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (!read_token_local(&cursor, interpreter, sizeof(interpreter))) {
        return 0;
    }
    cursor = skip_spaces_local(cursor);
    copy_line_local(extra, cursor != NULL ? cursor : "", sizeof(extra));

    copy_line_local(out, interpreter, out_size);
    out_len = str_len_local(out);
    if (extra[0] != '\0') {
        if (out_len + str_len_local(extra) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; extra[i] != '\0'; i++) {
            out[out_len++] = extra[i];
        }
        out[out_len] = '\0';
    }
    if (out_len + str_len_local(path) + 2u >= out_size) {
        return 0;
    }
    out[out_len++] = ' ';
    for (i = 0; path[i] != '\0'; i++) {
        out[out_len++] = path[i];
    }

    cursor = original_line;
    if (!read_token_local(&cursor, interpreter, sizeof(interpreter))) {
        out[out_len] = '\0';
        return 1;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        if (out_len + str_len_local(cursor) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; cursor[i] != '\0'; i++) {
            out[out_len++] = cursor[i];
        }
    }
    out[out_len] = '\0';
    return 1;
}

int ush_try_shebang_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_shebang_command(path, line, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_script_ansi_error, "script exec failed: ");
    ush_write_colored_err(ush_script_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}

int ush_try_sh_script_command(char *cwd,
                                     const char *line,
                                     const char *path,
                                     int background) {
    char arg_storage[10][USH_LINE_MAX + 1];
    char *argv[10];
    const char *cursor = line;
    int argc = 0;

    if (path == NULL || !ends_with_ignore_case_local(path, ".sh")) {
        return 0;
    }
    if (background) {
        write_err_str("background: shell script cannot run in background\n");
        return 1;
    }
    while (argc < 10 && read_token_local(&cursor,
                                         arg_storage[argc],
                                         sizeof(arg_storage[argc]))) {
        argv[argc] = arg_storage[argc];
        argc++;
    }
    cursor = skip_spaces_local(cursor);
    if (argc == 0 || (cursor != NULL && *cursor != '\0')) {
        write_err_str("script: too many or invalid arguments\n");
        return 1;
    }
    argv[0] = (char *)path;
    return ush_run_script_file(cwd, path, argc, argv);
}

