#include "kernel/public/proc/process_command.h"

#include "abi/syscall_abi.h"
#include "kernel/public/proc/process.h"

static uint32_t process_command_text_len(const char *text) {
    uint32_t len = 0u;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static void process_command_copy_text(char *dst,
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

static uint32_t process_command_find_last_slash(const char *text) {
    uint32_t i = 0u;
    uint32_t last = 0u;

    while (text != 0 && text[i] != '\0') {
        if (text[i] == '/') {
            last = i;
        }
        i++;
    }
    return last;
}

static void process_command_path_pop_segment(char *path) {
    uint32_t len;
    uint32_t last;

    if (path == 0) {
        return;
    }
    len = process_command_text_len(path);
    if (len <= 1u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    last = process_command_find_last_slash(path);
    if (last == 0u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    path[last] = '\0';
}

static int process_command_path_append_segment(char *path,
                                               uint32_t path_size,
                                               const char *segment,
                                               uint32_t segment_len) {
    uint32_t len = process_command_text_len(path);

    if (path == 0 || segment == 0 || segment_len == 0u) {
        return 0;
    }
    if (len == 0u || len >= path_size) {
        return 0;
    }
    if (!(len == 1u && path[0] == '/')) {
        if (len + 1u >= path_size) {
            return 0;
        }
        path[len++] = '/';
    }
    if (len + segment_len >= path_size) {
        return 0;
    }
    for (uint32_t i = 0u; i < segment_len; i++) {
        path[len + i] = segment[i];
    }
    path[len + segment_len] = '\0';
    return 1;
}

static int process_command_name_needs_path(const char *name) {
    uint32_t i = 0u;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.') {
        return 1;
    }
    while (name[i] != '\0') {
        if (name[i] == '/') {
            return 1;
        }
        i++;
    }
    return 0;
}

static int process_command_resolve_process_path(const struct process *process,
                                                const char *input,
                                                char *out,
                                                uint32_t out_size) {
    uint32_t pos = 0u;

    if (input == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        pos = 1u;
    } else {
        if (process == 0) {
            return 0;
        }
        process_command_copy_text(out, out_size, process_cwd(process));
    }
    if (input[0] == '\0' || (input[0] == '.' && input[1] == '\0')) {
        return 1;
    }
    while (input[pos] != '\0') {
        char segment[NOS_NAME_BUFFER_SIZE];
        uint32_t seg_len = 0u;

        while (input[pos] == '/') {
            pos++;
        }
        if (input[pos] == '\0') {
            break;
        }
        while (input[pos] != '\0' && input[pos] != '/') {
            if (seg_len + 1u >= sizeof(segment)) {
                return 0;
            }
            segment[seg_len++] = input[pos++];
        }
        segment[seg_len] = '\0';
        if (seg_len == 1u && segment[0] == '.') {
            continue;
        }
        if (seg_len == 2u && segment[0] == '.' && segment[1] == '.') {
            process_command_path_pop_segment(out);
            continue;
        }
        if (!process_command_path_append_segment(out,
                                                 out_size,
                                                 segment,
                                                 seg_len)) {
            return 0;
        }
    }
    return 1;
}

int process_command_resolve_exec_line(const struct process *process,
                                      char *line,
                                      uint32_t line_size) {
    char token[NOS_PATH_BUFFER_SIZE];
    char resolved[NOS_PATH_BUFFER_SIZE];
    char original[NOS_TTY_LINE_BUFFER_SIZE];
    uint32_t rest_pos = 0u;
    uint32_t token_len = 0u;
    uint32_t resolved_len;
    uint32_t rest_len;

    if (line == 0 || line_size == 0u) {
        return 0;
    }
    while (line[rest_pos] == ' ' || line[rest_pos] == '\t') {
        rest_pos++;
    }
    process_command_copy_text(original, sizeof(original), line);
    while (line[rest_pos + token_len] != '\0' &&
           line[rest_pos + token_len] != ' ' &&
           line[rest_pos + token_len] != '\t') {
        if (token_len + 1u >= sizeof(token)) {
            return 0;
        }
        token[token_len] = line[rest_pos + token_len];
        token_len++;
    }
    token[token_len] = '\0';
    if (token_len == 0u || !process_command_name_needs_path(token)) {
        return 1;
    }
    if (!process_command_resolve_process_path(process,
                                              token,
                                              resolved,
                                              sizeof(resolved))) {
        return 0;
    }
    resolved_len = process_command_text_len(resolved);
    rest_len = process_command_text_len(line + rest_pos + token_len);
    if (rest_pos + resolved_len + rest_len + 1u > line_size) {
        return 0;
    }
    for (uint32_t i = 0u; i < resolved_len; i++) {
        line[rest_pos + i] = resolved[i];
    }
    for (uint32_t i = 0u; i <= rest_len; i++) {
        line[rest_pos + resolved_len + i] =
            original[rest_pos + token_len + i];
    }
    return 1;
}

int process_command_parse_argv(char *command,
                               const char *argv[],
                               uint32_t argv_max,
                               int *argc_out) {
    int argc = 0;
    char *cursor = command;

    if (command == 0 || argv == 0 || argc_out == 0 || argv_max == 0u) {
        return 0;
    }
    while (*cursor != '\0' && (uint32_t)argc < argv_max) {
        char *out;
        int single_quote = 0;
        int double_quote = 0;

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        out = cursor;
        argv[argc++] = out;
        while (*cursor != '\0') {
            char ch = *cursor;

            if (!single_quote && ch == '\\') {
                cursor++;
                if (*cursor == '\0') {
                    break;
                }
                *out++ = *cursor++;
                continue;
            }
            if (!double_quote && ch == '\'') {
                single_quote = !single_quote;
                cursor++;
                continue;
            }
            if (!single_quote && ch == '"') {
                double_quote = !double_quote;
                cursor++;
                continue;
            }
            if (!single_quote && !double_quote &&
                (ch == ' ' || ch == '\t')) {
                cursor++;
                break;
            }
            *out++ = *cursor++;
        }
        *out = '\0';
    }
    *argc_out = argc;
    return argc != 0;
}
