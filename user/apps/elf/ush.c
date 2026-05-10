#include "user/apps/elf/ush_shared.h"

static int streq_local(const char *a, const char *b) {
    uint32_t i = 0;

    for (;;) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
}

static uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_line_local(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (dst == 0 || max_len == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int is_space_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trim_in_place_local(char *text) {
    uint32_t start = 0;
    uint32_t end = str_len_local(text);
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[start] != '\0' && is_space_local(text[start])) {
        start++;
    }
    while (end > start && is_space_local(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

static char ush_ascii_tolower_local(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static const char *ush_basename_local(const char *path) {
    const char *base = path;
    uint32_t i = 0;

    if (path == 0) {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
        i++;
    }
    return base;
}

static void copy_lowercase_line_local(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = ush_ascii_tolower_local(src[i]);
        i++;
    }
    dst[i] = '\0';
}

int ush_build_invoked_command_line(int argc, char **argv, char *out, uint32_t out_size) {
    char program[32];
    const char *base;
    uint32_t i;

    if (argc <= 0 || argv == 0 || out == 0 || out_size == 0) {
        return 0;
    }
    base = ush_basename_local(argv[0]);
    copy_lowercase_line_local(program, sizeof(program), base);
    if (program[0] == '\0' || streq_local(program, "ush") || streq_local(program, "ush.elf")) {
        return 0;
    }
    copy_line_local(out, program, out_size);
    for (i = 1u; i < (uint32_t)argc; i++) {
        uint32_t arg_len;
        uint32_t out_len = str_len_local(out);
        uint32_t j;

        if (argv[i] == 0 || argv[i][0] == '\0') {
            continue;
        }
        arg_len = str_len_local(argv[i]);
        if (out_len + arg_len + 2u >= out_size) {
            write_err_str("ush: command line too long\n");
            return 0;
        }
        out[out_len++] = ' ';
        for (j = 0; j < arg_len; j++) {
            out[out_len++] = argv[i][j];
        }
        out[out_len] = '\0';
    }
    return 1;
}

static void ush_function_body_trim_tail_local(char *body) {
    uint32_t len;

    if (body == 0) {
        return;
    }
    trim_in_place_local(body);
    len = str_len_local(body);
    while (len != 0) {
        while (len != 0 && is_space_local(body[len - 1u])) {
            body[--len] = '\0';
        }
        if (len == 0 || body[len - 1u] != ';') {
            break;
        }
        body[--len] = '\0';
    }
    trim_in_place_local(body);
}

static int ush_function_body_append_local(char *body, uint32_t body_size, const char *line) {
    char part[USH_LINE_MAX + 1];
    uint32_t body_len;
    uint32_t part_len;
    uint32_t i;

    if (body == 0 || body_size == 0 || line == 0) {
        return 0;
    }
    copy_line_local(part, line, sizeof(part));
    trim_in_place_local(part);
    part_len = str_len_local(part);
    while (part_len != 0) {
        while (part_len != 0 && is_space_local(part[part_len - 1u])) {
            part[--part_len] = '\0';
        }
        if (part_len == 0 || part[part_len - 1u] != ';') {
            break;
        }
        part[--part_len] = '\0';
    }
    trim_in_place_local(part);
    if (part[0] == '\0') {
        return 1;
    }
    body_len = str_len_local(body);
    part_len = str_len_local(part);
    if (body_len != 0) {
        if (body_len + 2u >= body_size) {
            return 0;
        }
        body[body_len++] = ';';
        body[body_len++] = ' ';
    }
    if (body_len + part_len + 1u > body_size) {
        return 0;
    }
    for (i = 0; i < part_len; i++) {
        body[body_len++] = part[i];
    }
    body[body_len] = '\0';
    return 1;
}

static int ush_parse_function_start_local(const char *line,
                                          char *name,
                                          uint32_t name_size,
                                          char *body,
                                          uint32_t body_size,
                                          int *complete_out) {
    char work[USH_LINE_MAX + 1];
    uint32_t brace = 0xffffffffu;
    uint32_t end;
    uint32_t i;

    if (line == 0 || name == 0 || body == 0 || complete_out == 0 || name_size == 0 || body_size == 0) {
        return 0;
    }
    copy_line_local(work, line, sizeof(work));
    trim_in_place_local(work);
    for (i = 0; work[i] != '\0'; i++) {
        if (work[i] == '{') {
            brace = i;
            break;
        }
    }
    if (brace == 0xffffffffu || brace == 0u) {
        return 0;
    }
    work[brace] = '\0';
    trim_in_place_local(work);
    if (!ush_var_name_valid_local(work)) {
        return 0;
    }
    copy_line_local(name, work, name_size);
    body[0] = '\0';
    *complete_out = 0;

    copy_line_local(work, line + brace + 1u, sizeof(work));
    trim_in_place_local(work);
    end = str_len_local(work);
    if (end != 0 && work[end - 1u] == '}') {
        work[end - 1u] = '\0';
        trim_in_place_local(work);
        *complete_out = 1;
    }
    if (!ush_function_body_append_local(body, body_size, work)) {
        return -1;
    }
    if (*complete_out) {
        ush_function_body_trim_tail_local(body);
    }
    return 1;
}

static int ush_function_close_line_local(const char *line) {
    char work[USH_LINE_MAX + 1];

    copy_line_local(work, line, sizeof(work));
    trim_in_place_local(work);
    return streq_local(work, "}");
}

static int ush_store_function_local(const char *name, char *body) {
    ush_function_body_trim_tail_local(body);
    if (body[0] == '\0') {
        write_err_str("function: empty body\n");
        return 0;
    }
    if (!ush_function_assign_local(name, body)) {
        write_err_str("function: could not store function\n");
        return 0;
    }
    return 1;
}

static int ush_config_parse_bool_local(const char *value, int *out) {
    if (value == 0 || out == 0) {
        return 0;
    }
    if (streq_local(value, "1") || streq_local(value, "true") ||
        streq_local(value, "yes") || streq_local(value, "on")) {
        *out = 1;
        return 1;
    }
    if (streq_local(value, "0") || streq_local(value, "false") ||
        streq_local(value, "no") || streq_local(value, "off")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static void ush_config_apply_pair_local(char *key, char *value) {
    int enabled;

    trim_in_place_local(key);
    trim_in_place_local(value);
    if (key[0] == '\0' || value[0] == '\0') {
        return;
    }
    if (streq_local(key, "function_recursion_limit") ||
        streq_local(key, "ush_function_recursion_limit")) {
        if (ush_config_parse_bool_local(value, &enabled)) {
            ush_function_recursion_limit_set(enabled);
        }
    }
}

static void ush_config_apply_line_local(char *line) {
    uint32_t i;

    trim_in_place_local(line);
    if (line[0] == '\0' || line[0] == '#') {
        return;
    }
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '#') {
            line[i] = '\0';
            break;
        }
    }
    trim_in_place_local(line);
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '=') {
            line[i] = '\0';
            ush_config_apply_pair_local(line, line + i + 1u);
            return;
        }
    }
}

static void ush_load_config_local(void) {
    char line[USH_LINE_MAX + 1];
    int fd = open("/SYSTEM/CONFIG/NOS.CFG", 0);

    if (fd < 0) {
        fd = open("SYSTEM/CONFIG/NOS.CFG", 0);
    }
    if (fd < 0) {
        fd = open("/NOS.CFG", 0);
    }
    if (fd < 0) {
        fd = open("NOS.CFG", 0);
    }
    if (fd < 0) {
        fd = open("/NEXOS.CFG", 0);
    }
    if (fd < 0) {
        fd = open("NEXOS.CFG", 0);
    }
    if (fd < 0) {
        return;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        ush_config_apply_line_local(line);
    }
    close((uint32_t)fd);
}

int ush_run_script_file(char *cwd, const char *path, int argc, char **argv) {
    char line[64];
    char function_name[USH_VAR_NAME_MAX + 1];
    char function_body[USH_FUNCTION_BODY_MAX + 1];
    struct ush_script_args_snapshot saved_args;
    int in_function = 0;
    uint32_t line_no = 0;
    int fd;

    if (path == 0 || path[0] == '\0') {
        write_err_str("ush: script path missing\n");
        return 1;
    }
    fd = open(path, 0);
    if (fd < 0) {
        write_err_str("ush: script open failed: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    ush_save_script_args_local(&saved_args);
    ush_set_script_args_local(argc, argv);
    for (;;) {
        uint32_t got = read_line((uint32_t)fd, line, sizeof(line));

        if (got == 0) {
            break;
        }
        line_no++;
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }
        if (line_no == 1u && line[0] == '#' && line[1] == '!') {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }
        if (in_function) {
            if (ush_function_close_line_local(line)) {
                if (!ush_store_function_local(function_name, function_body)) {
                    ush_restore_script_args_local(&saved_args);
                    close((uint32_t)fd);
                    return 1;
                }
                in_function = 0;
                continue;
            }
            if (!ush_function_body_append_local(function_body, sizeof(function_body), line)) {
                write_err_str("function: body too long\n");
                ush_restore_script_args_local(&saved_args);
                close((uint32_t)fd);
                return 1;
            }
            continue;
        }
        {
            int complete = 0;
            int def_rc = ush_parse_function_start_local(line,
                                                        function_name,
                                                        sizeof(function_name),
                                                        function_body,
                                                        sizeof(function_body),
                                                        &complete);

            if (def_rc < 0) {
                write_err_str("function: parse failed\n");
                ush_restore_script_args_local(&saved_args);
                close((uint32_t)fd);
                return 1;
            }
            if (def_rc > 0) {
                if (complete) {
                    if (!ush_store_function_local(function_name, function_body)) {
                        ush_restore_script_args_local(&saved_args);
                        close((uint32_t)fd);
                        return 1;
                    }
                } else {
                    in_function = 1;
                }
                continue;
            }
        }
        if (ush_execute_line(cwd, line) < 0) {
            ush_restore_script_args_local(&saved_args);
            close((uint32_t)fd);
            return 0;
        }
    }
    ush_restore_script_args_local(&saved_args);
    close((uint32_t)fd);
    return 0;
}

static void ush_bind_interactive_stdio(void) {
    int tty_fd = open("/dev/tty", 0);

    if (tty_fd < 0) {
        return;
    }
    if (tty_fd != STDIN_FILENO) {
        (void)dup2(tty_fd, STDIN_FILENO);
    }
    if (tty_fd != STDOUT_FILENO) {
        (void)dup2(tty_fd, STDOUT_FILENO);
    }
    if (tty_fd != STDERR_FILENO) {
        (void)dup2(tty_fd, STDERR_FILENO);
    }
    if (tty_fd > STDERR_FILENO) {
        close((uint32_t)tty_fd);
    }
}

int main(int argc, char **argv) {
    struct ush_editor editor = {0};
    char invoked_line[64];
    char line[64];
    char cwd[64];
    char function_name[USH_VAR_NAME_MAX + 1];
    char function_body[USH_FUNCTION_BODY_MAX + 1];

    ush_refresh_cwd_local(cwd, sizeof(cwd));
    ush_init_vars_local(cwd);
    ush_load_config_local();
    if (argc > 1 && (streq_local(ush_basename_local(argv[0]), "ush") ||
                     streq_local(ush_basename_local(argv[0]), "USH.ELF") ||
                     streq_local(ush_basename_local(argv[0]), "ush.elf"))) {
        return ush_run_script_file(cwd, argv[1], argc - 1, argv + 1);
    }
    if (ush_build_invoked_command_line(argc, argv, invoked_line, sizeof(invoked_line))) {
        (void)ush_execute_line(cwd, invoked_line);
        return 0;
    }
    ush_bind_interactive_stdio();
    ush_prompt_sync(cwd);
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
        {
            int complete = 0;
            int def_rc = ush_parse_function_start_local(line,
                                                        function_name,
                                                        sizeof(function_name),
                                                        function_body,
                                                        sizeof(function_body),
                                                        &complete);

            if (def_rc < 0) {
                write_err_str("function: parse failed\n");
                continue;
            }
            if (def_rc > 0) {
                while (!complete) {
                    ush_prompt_override("func> ");
                    write_str("func> ");
                    if (!read_line_chars(&editor, line, sizeof(line))) {
                        ush_prompt_override(NULL);
                        ush_write_error("read failed\n");
                        exit_with_code(1);
                    }
                    ush_prompt_override(NULL);
                    if (ush_function_close_line_local(line)) {
                        complete = 1;
                        break;
                    }
                    if (!ush_function_body_append_local(function_body, sizeof(function_body), line)) {
                        write_err_str("function: body too long\n");
                        break;
                    }
                }
                if (complete) {
                    (void)ush_store_function_local(function_name, function_body);
                }
                continue;
            }
        }
        if (ush_execute_line(cwd, line) < 0) {
            return 0;
        }
    }
}
