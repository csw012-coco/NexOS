#include "user/apps/elf/ush/ush_shared.h"

static uint32_t str_len_script_local(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_line_script_local(char *dst, const char *src, uint32_t max_len) {
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

static int is_space_script_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int streq_script_local(const char *a, const char *b) {
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

static void trim_script_local(char *text) {
    uint32_t start = 0;
    uint32_t end;
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    end = str_len_script_local(text);
    while (text[start] != '\0' && is_space_script_local(text[start])) {
        start++;
    }
    while (end > start && is_space_script_local(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

static void ush_function_body_trim_tail_local(char *body) {
    uint32_t len;

    if (body == 0) {
        return;
    }
    trim_script_local(body);
    len = str_len_script_local(body);
    while (len != 0) {
        while (len != 0 && is_space_script_local(body[len - 1u])) {
            body[--len] = '\0';
        }
        if (len == 0 || body[len - 1u] != ';') {
            break;
        }
        body[--len] = '\0';
    }
    trim_script_local(body);
}

int ush_function_body_append_local(char *body, uint32_t body_size, const char *line) {
    char part[USH_LINE_MAX + 1];
    uint32_t body_len;
    uint32_t part_len;
    uint32_t i;

    if (body == 0 || body_size == 0 || line == 0) {
        return 0;
    }
    copy_line_script_local(part, line, sizeof(part));
    trim_script_local(part);
    part_len = str_len_script_local(part);
    while (part_len != 0) {
        while (part_len != 0 && is_space_script_local(part[part_len - 1u])) {
            part[--part_len] = '\0';
        }
        if (part_len == 0 || part[part_len - 1u] != ';') {
            break;
        }
        part[--part_len] = '\0';
    }
    trim_script_local(part);
    if (part[0] == '\0') {
        return 1;
    }
    body_len = str_len_script_local(body);
    part_len = str_len_script_local(part);
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

int ush_parse_function_start_local(const char *line,
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
    copy_line_script_local(work, line, sizeof(work));
    trim_script_local(work);
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
    trim_script_local(work);
    if (!ush_var_name_valid_local(work)) {
        return 0;
    }
    copy_line_script_local(name, work, name_size);
    body[0] = '\0';
    *complete_out = 0;

    copy_line_script_local(work, line + brace + 1u, sizeof(work));
    trim_script_local(work);
    end = str_len_script_local(work);
    if (end != 0 && work[end - 1u] == '}') {
        work[end - 1u] = '\0';
        trim_script_local(work);
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

int ush_function_close_line_local(const char *line) {
    char work[USH_LINE_MAX + 1];

    copy_line_script_local(work, line, sizeof(work));
    trim_script_local(work);
    return streq_script_local(work, "}");
}

int ush_store_function_local(const char *name, char *body) {
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
