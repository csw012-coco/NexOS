#include "user/apps/elf/ush/ush_exec_internal.h"

int ush_program_name_needs_path(const char *name, int resolve_dot_name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.' || contains_char_local(name, '/')) {
        return 1;
    }
    return resolve_dot_name && contains_char_local(name, '.');
}

int ush_build_program_command(const char *arg,
                              const char *verb,
                              int resolve_dot_name,
                              char *out,
                              uint32_t out_size) {
    char token[64];
    const char *rest = arg;
    const char *name = token;
    uint32_t out_len = 0;

    if (out == NULL || out_size == 0) {
        return 0;
    }
    if (!read_token_local(&rest, token, sizeof(token))) {
        write_err_str("usage: ");
        write_err_str(verb);
        write_err_str(" <name> [args]\n");
        out[0] = '\0';
        return 0;
    }

    (void)resolve_dot_name;

    copy_line_local(out, name, out_size);
    out_len = str_len_local(out);
    rest = skip_spaces_local(rest);
    if (rest != NULL && *rest != '\0') {
        uint32_t i = 0;

        if (out_len + 1u >= out_size) {
            write_err_str(verb);
            write_err_str(": command line too long\n");
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = ' ';
        while (rest[i] != '\0') {
            if (out_len + 1u >= out_size) {
                write_err_str(verb);
                write_err_str(": command line too long\n");
                out[0] = '\0';
                return 0;
            }
            out[out_len++] = rest[i++];
        }
        out[out_len] = '\0';
    }

    return 1;
}

int ush_build_cmd_search_command_from(const char *line,
                                      const char *cmd_dir,
                                      int lower_name,
                                      char *out,
                                      uint32_t out_size) {
    char token[64];
    const char *rest = line;
    uint32_t out_len;

    if (out == NULL || out_size == 0 || cmd_dir == NULL) {
        return 0;
    }
    if (!read_token_local(&rest, token, sizeof(token))) {
        out[0] = '\0';
        return 0;
    }
    if (lower_name) {
        lower_in_place_local(token);
    } else {
        upper_in_place_local(token);
    }

    if (snprintf(out, out_size, "%s/%s", cmd_dir, token) < 0 || out[0] == '\0') {
        out[0] = '\0';
        return 0;
    }
    out_len = str_len_local(out);
    rest = skip_spaces_local(rest);
    if (rest != NULL && *rest != '\0') {
        uint32_t i = 0;

        if (out_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = ' ';
        while (rest[i] != '\0') {
            if (out_len + 1u >= out_size) {
                out[0] = '\0';
                return 0;
            }
            out[out_len++] = rest[i++];
        }
        out[out_len] = '\0';
    }
    return 1;
}

int ush_build_cmd_search_command(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 0, out, out_size);
}

int ush_build_cmd_search_command_lower(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 1, out, out_size);
}

int ush_build_prefixed_command(const char *prefix,
                               const char *line,
                               char *out,
                               uint32_t out_size) {
    uint32_t out_len;
    uint32_t i = 0;

    if (prefix == NULL || line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    copy_line_local(out, prefix, out_size);
    out_len = str_len_local(out);
    if (out_len == 0 || out_len + 1u >= out_size) {
        out[0] = '\0';
        return 0;
    }
    out[out_len++] = ' ';
    while (line[i] != '\0') {
        if (out_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = line[i++];
    }
    out[out_len] = '\0';
    return 1;
}

int ush_build_action_command(const char *line, char *out, uint32_t out_size) {
    uint32_t prefix_len;
    uint32_t i = 0;

    if (line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    prefix_len = (uint32_t)snprintf(out, out_size, "/cmd/action run ");
    if (prefix_len == 0u || prefix_len >= out_size) {
        out[0] = '\0';
        return 0;
    }
    while (line[i] != '\0') {
        if (prefix_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[prefix_len++] = line[i++];
    }
    out[prefix_len] = '\0';
    return 1;
}
