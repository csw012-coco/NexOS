#include "user/apps/elf/ush_shared.h"

static const char ush_ansi_reset[] = "\x1b[0m";
static const char ush_ansi_error[] = "\x1b[1;31m";
static const char ush_ansi_value[] = "\x1b[1;33m";

enum {
    USH_PIPELINE_STAGE_MAX = 8u,
    USH_FUNCTION_CALL_DEPTH_MAX = 4u,
    USH_ACTION_CAP_DEVICE_READ = 1u << 16,
    USH_ACTION_CAP_DEVICE_WRITE = 1u << 21,
    USH_ACTION_CAP_ALL = (1u << 22) - 1u,
    USH_ACTION_POLICY_FILE_MAX = 768u
};

static uint32_t g_ush_function_call_depth;
static int g_ush_function_recursion_limit_enabled = 1;
static const char *g_ush_action_caps_path = "/HOME/ACTION.CAPS";

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

static int starts_with_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int contains_char_local(const char *text, char ch) {
    uint32_t i = 0;

    while (text[i] != '\0') {
        if (text[i] == ch) {
            return 1;
        }
        i++;
    }
    return 0;
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

    if (max_len == 0) {
        return;
    }
    while (src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ush_policy_read_word(const char **cursor_io, char *out, uint32_t out_size) {
    const char *cursor;
    uint32_t pos = 0;

    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (cursor_io == NULL || *cursor_io == NULL) {
        return;
    }
    cursor = *cursor_io;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' &&
           *cursor != ' ' && *cursor != '\t') {
        if (pos + 1u < out_size) {
            out[pos++] = *cursor;
        }
        cursor++;
    }
    out[pos] = '\0';
    *cursor_io = cursor;
}

static int ush_parse_cap_mask_local(const char *text, uint32_t *mask_out) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || mask_out == NULL || text[0] == '\0') {
        return 0;
    }
    if (streq_local(text, "all")) {
        *mask_out = USH_ACTION_CAP_ALL;
        return 1;
    }
    if (streq_local(text, "device.read")) {
        *mask_out = USH_ACTION_CAP_DEVICE_READ;
        return 1;
    }
    if (streq_local(text, "device.write")) {
        *mask_out = USH_ACTION_CAP_DEVICE_WRITE;
        return 1;
    }
    value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return 0;
    }
    *mask_out = (uint32_t)value;
    return 1;
}

static void ush_policy_apply_line(uint32_t *mask_io, const char *line) {
    char verb[16];
    char kind[16];
    char value[32];
    uint32_t cap = 0;
    const char *cursor = line;

    if (mask_io == NULL || line == NULL) {
        return;
    }
    ush_policy_read_word(&cursor, verb, sizeof(verb));
    if (verb[0] == '\0' || verb[0] == '#') {
        return;
    }
    if (verb[0] >= '0' && verb[0] <= '9') {
        if (ush_parse_cap_mask_local(verb, &cap)) {
            *mask_io = cap & USH_ACTION_CAP_ALL;
        }
        return;
    }
    ush_policy_read_word(&cursor, kind, sizeof(kind));
    ush_policy_read_word(&cursor, value, sizeof(value));
    if (streq_local(verb, "mask")) {
        if (ush_parse_cap_mask_local(kind, &cap)) {
            *mask_io = cap & USH_ACTION_CAP_ALL;
        }
        return;
    }
    if (streq_local(kind, "cap") && ush_parse_cap_mask_local(value, &cap)) {
        if (streq_local(verb, "allow")) {
            *mask_io |= cap;
        } else if (streq_local(verb, "deny")) {
            *mask_io &= ~cap;
        }
    }
}

static uint32_t ush_policy_load_mask(void) {
    char buffer[USH_ACTION_POLICY_FILE_MAX];
    char line[96];
    uint32_t mask = USH_ACTION_CAP_ALL;
    uint32_t pos = 0;
    int fd = open(g_ush_action_caps_path, 0);
    uint32_t got;

    if (fd < 0) {
        return mask;
    }
    got = (uint32_t)read(fd, buffer, sizeof(buffer) - 1u);
    close(fd);
    buffer[got] = '\0';
    while (buffer[pos] != '\0') {
        uint32_t line_pos = 0;

        while (buffer[pos] != '\0' && buffer[pos] != '\n' && line_pos + 1u < sizeof(line)) {
            line[line_pos++] = buffer[pos++];
        }
        while (buffer[pos] != '\0' && buffer[pos] != '\n') {
            pos++;
        }
        if (buffer[pos] == '\n') {
            pos++;
        }
        line[line_pos] = '\0';
        ush_policy_apply_line(&mask, line);
    }
    return mask;
}

static int ush_path_targets_devfs(const char *cwd, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (starts_with_local(path, "/dev/")) {
        return 1;
    }
    return path[0] != '/' && cwd != NULL && streq_local(cwd, "/dev");
}

static int ush_check_device_redirect_cap(const char *cwd, const char *path, uint32_t cap, const char *op) {
    if (!ush_path_targets_devfs(cwd, path)) {
        return 1;
    }
    if ((ush_policy_load_mask() & cap) != 0u) {
        return 1;
    }
    write_err_str("redirect: denied ");
    write_err_str(op);
    write_err_str(": ");
    write_err_str(path);
    write_err_str("\n");
    return 0;
}

static void ush_write_colored_err(const char *ansi, const char *text) {
    write_err_str(ansi);
    write_err_str(text);
    write_err_str(ush_ansi_reset);
}

static char to_upper_ascii_local(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static void upper_in_place_local(char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[i] != '\0') {
        text[i] = to_upper_ascii_local(text[i]);
        i++;
    }
}

static char to_lower_ascii_local(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }
    return ch;
}

static void lower_in_place_local(char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[i] != '\0') {
        text[i] = to_lower_ascii_local(text[i]);
        i++;
    }
}

static int ends_with_ignore_case_local(const char *text, const char *suffix) {
    uint32_t text_len = str_len_local(text);
    uint32_t suffix_len = str_len_local(suffix);
    uint32_t i;

    if (suffix_len > text_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; i++) {
        if (to_upper_ascii_local(text[text_len - suffix_len + i]) != to_upper_ascii_local(suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_space_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static const char *skip_spaces_local(const char *text) {
    while (text != NULL && is_space_local(*text)) {
        text++;
    }
    return text;
}

static int read_token_local(const char **text_io, char *out, uint32_t out_size);
static int ush_spawn_command_local(const char *command, uint32_t mode, int background);

static int ush_line_has_function_operator_local(const char *line) {
    uint32_t pos = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL) {
        return 0;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            pos += 2u;
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        } else if (!single_quote && !double_quote &&
                   (line[pos] == ';' || line[pos] == '|' || line[pos] == '&' ||
                    line[pos] == '<' || line[pos] == '>')) {
            return 1;
        }
        pos++;
    }
    return 0;
}

static int ush_try_function_call_local(char *cwd, const char *line, int require_plain_line, int *handled_out) {
    char token[64];
    const char *cursor = line;
    const char *function_body;
    int rc;

    if (handled_out == NULL) {
        return 1;
    }
    *handled_out = 0;
    if (require_plain_line && ush_line_has_function_operator_local(line)) {
        return 0;
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        return 0;
    }
    function_body = ush_function_lookup_local(token);
    if (function_body == NULL) {
        return 0;
    }
    *handled_out = 1;
    if (g_ush_function_recursion_limit_enabled &&
        g_ush_function_call_depth >= USH_FUNCTION_CALL_DEPTH_MAX) {
        write_err_str("function: recursion limit\n");
        return 1;
    }
    g_ush_function_call_depth++;
    rc = ush_execute_line(cwd, function_body);
    g_ush_function_call_depth--;
    return rc;
}

void ush_function_recursion_limit_set(int enabled) {
    g_ush_function_recursion_limit_enabled = enabled ? 1 : 0;
}

static void trim_in_place_local(char *text) {
    uint32_t start = 0;
    uint32_t end = str_len_local(text);
    uint32_t i = 0;

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

static int ush_is_background_amp_local(const char *line, uint32_t pos) {
    uint32_t prev;

    if (line == NULL || line[pos] != '&' || line[pos + 1u] == '&') {
        return 0;
    }
    prev = pos;
    while (prev > 0u && is_space_local(line[prev - 1u])) {
        prev--;
    }
    if (prev > 0u && line[prev - 1u] == '>') {
        return 0;
    }
    return 1;
}

static int ush_split_andif(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    uint32_t pos = 0;
    uint32_t left_len = 0;
    uint32_t right_len = 0;
    int found = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return -1;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            if (!found) {
                if (left_len + 2u >= left_size) {
                    return -1;
                }
                left[left_len++] = line[pos++];
                left[left_len++] = line[pos++];
            } else {
                if (right_len + 2u >= right_size) {
                    return -1;
                }
                right[right_len++] = line[pos++];
                right[right_len++] = line[pos++];
            }
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        }
        if (!single_quote && !double_quote && line[pos] == '&' && line[pos + 1u] == '&') {
            if (found) {
                return -1;
            }
            found = 1;
            pos += 2u;
            continue;
        }
        if (!found) {
            if (left_len + 1u >= left_size) {
                return -1;
            }
            left[left_len++] = line[pos];
        } else {
            if (right_len + 1u >= right_size) {
                return -1;
            }
            right[right_len++] = line[pos];
        }
        pos++;
    }
    left[left_len] = '\0';
    right[right_len] = '\0';
    trim_in_place_local(left);
    trim_in_place_local(right);
    if (!found) {
        right[0] = '\0';
        return 0;
    }
    if (left[0] == '\0' || right[0] == '\0') {
        return -1;
    }
    return 1;
}

static int ush_split_orif(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    uint32_t pos = 0;
    uint32_t left_len = 0;
    uint32_t right_len = 0;
    int found = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return -1;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            if (!found) {
                if (left_len + 2u >= left_size) {
                    return -1;
                }
                left[left_len++] = line[pos++];
                left[left_len++] = line[pos++];
            } else {
                if (right_len + 2u >= right_size) {
                    return -1;
                }
                right[right_len++] = line[pos++];
                right[right_len++] = line[pos++];
            }
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        }
        if (!single_quote && !double_quote && line[pos] == '|' && line[pos + 1u] == '|') {
            if (found) {
                return -1;
            }
            found = 1;
            pos += 2u;
            continue;
        }
        if (!found) {
            if (left_len + 1u >= left_size) {
                return -1;
            }
            left[left_len++] = line[pos];
        } else {
            if (right_len + 1u >= right_size) {
                return -1;
            }
            right[right_len++] = line[pos];
        }
        pos++;
    }
    left[left_len] = '\0';
    right[right_len] = '\0';
    trim_in_place_local(left);
    trim_in_place_local(right);
    if (!found) {
        right[0] = '\0';
        return 0;
    }
    if (left[0] == '\0' || right[0] == '\0') {
        return -1;
    }
    return 1;
}

static int ush_split_sequence(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    uint32_t pos = 0;
    uint32_t left_len = 0;
    uint32_t right_len = 0;
    int found = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return -1;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            if (!found) {
                if (left_len + 2u >= left_size) {
                    return -1;
                }
                left[left_len++] = line[pos++];
                left[left_len++] = line[pos++];
            } else {
                if (right_len + 2u >= right_size) {
                    return -1;
                }
                right[right_len++] = line[pos++];
                right[right_len++] = line[pos++];
            }
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        }
        if (!single_quote && !double_quote && line[pos] == ';') {
            if (found) {
                return -1;
            }
            found = 1;
            pos++;
            continue;
        }
        if (!found) {
            if (left_len + 1u >= left_size) {
                return -1;
            }
            left[left_len++] = line[pos];
        } else {
            if (right_len + 1u >= right_size) {
                return -1;
            }
            right[right_len++] = line[pos];
        }
        pos++;
    }
    left[left_len] = '\0';
    right[right_len] = '\0';
    trim_in_place_local(left);
    trim_in_place_local(right);
    if (!found) {
        right[0] = '\0';
        return 0;
    }
    if (left[0] == '\0' || right[0] == '\0') {
        return -1;
    }
    return 1;
}

static int ush_split_background_list(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    uint32_t pos = 0;
    uint32_t left_len = 0;
    uint32_t right_len = 0;
    int found = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return -1;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            if (!found) {
                if (left_len + 2u >= left_size) {
                    return -1;
                }
                left[left_len++] = line[pos++];
                left[left_len++] = line[pos++];
            } else {
                if (right_len + 2u >= right_size) {
                    return -1;
                }
                right[right_len++] = line[pos++];
                right[right_len++] = line[pos++];
            }
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        }
        if (!single_quote && !double_quote && ush_is_background_amp_local(line, pos)) {
            if (found) {
                return -1;
            }
            found = 1;
            pos++;
            continue;
        }
        if (!found) {
            if (left_len + 1u >= left_size) {
                return -1;
            }
            left[left_len++] = line[pos];
        } else {
            if (right_len + 1u >= right_size) {
                return -1;
            }
            right[right_len++] = line[pos];
        }
        pos++;
    }
    left[left_len] = '\0';
    right[right_len] = '\0';
    trim_in_place_local(left);
    trim_in_place_local(right);
    if (single_quote || double_quote) {
        return -1;
    }
    if (!found) {
        right[0] = '\0';
        return 0;
    }
    if (left[0] == '\0') {
        return -1;
    }
    return right[0] == '\0' ? 0 : 1;
}

static int ush_strip_trailing_background_local(const char *line,
                                               char *out,
                                               uint32_t out_size,
                                               int *background_out) {
    uint32_t pos = 0;
    uint32_t amp_pos = 0xffffffffu;
    uint32_t out_len;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || out == NULL || out_size == 0 || background_out == NULL) {
        return -1;
    }
    *background_out = 0;
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            pos += 2u;
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        } else if (!single_quote && !double_quote && ush_is_background_amp_local(line, pos)) {
            amp_pos = pos;
            pos++;
            while (is_space_local(line[pos])) {
                pos++;
            }
            if (line[pos] != '\0') {
                return -1;
            }
            break;
        }
        pos++;
    }
    if (single_quote || double_quote) {
        return -1;
    }
    if (amp_pos == 0xffffffffu) {
        copy_line_local(out, line, out_size);
        return 0;
    }
    if (amp_pos + 1u > out_size) {
        return -1;
    }
    for (out_len = 0; out_len < amp_pos; out_len++) {
        out[out_len] = line[out_len];
    }
    out[out_len] = '\0';
    trim_in_place_local(out);
    if (out[0] == '\0') {
        return -1;
    }
    *background_out = 1;
    return 1;
}

static int read_token_local(const char **text_io, char *out, uint32_t out_size) {
    uint32_t len = 0;
    const char *text;

    if (text_io == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    text = skip_spaces_local(*text_io);
    if (text == NULL || *text == '\0') {
        out[0] = '\0';
        *text_io = text;
        return 0;
    }
    while (*text != '\0' && !is_space_local(*text)) {
        if (len + 1u >= out_size) {
            return 0;
        }
        out[len++] = *text++;
    }
    out[len] = '\0';
    *text_io = text;
    return 1;
}

static int ush_parse_exit_code_local(const char *text, uint64_t *code_out) {
    char token[32];
    const char *cursor = skip_spaces_local(text);
    char *end = 0;
    unsigned long value;

    if (code_out == NULL) {
        return 0;
    }
    if (cursor == NULL || *cursor == '\0') {
        *code_out = 0u;
        return 1;
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        return 0;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        return 0;
    }
    value = strtoul(token, &end, 0);
    if (end == token || *end != '\0') {
        return 0;
    }
    *code_out = (uint64_t)value;
    return 1;
}

static int ush_source_script_local(char *cwd, const char *text) {
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

static int ush_session_load_local(char *cwd, const char *text) {
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
    if (snprintf(path, sizeof(path), "/SYSTEM/SESSION/images/%s.ush", name) < 0) {
        write_err_str("session: restore path failed\n");
        return 1;
    }
    argv[0] = path;
    return ush_run_script_file(cwd, path, 1, argv);
}

static int ush_program_name_needs_path(const char *name, int resolve_dot_name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.' || contains_char_local(name, '/')) {
        return 1;
    }
    return resolve_dot_name && contains_char_local(name, '.');
}

static int ush_build_program_command(const char *arg,
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

static int ush_build_cmd_search_command_from(const char *line,
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

static int ush_build_cmd_search_command(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 0, out, out_size);
}

static int ush_build_cmd_search_command_lower(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 1, out, out_size);
}

static int ush_build_action_command(const char *line, char *out, uint32_t out_size) {
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

static int ush_try_shebang_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_shebang_command(path, line, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_ansi_error, "script exec failed: ");
    ush_write_colored_err(ush_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}

static int ush_parse_wav_command(const char *original_line,
                                 const char *path,
                                 char *out,
                                 uint32_t out_size) {
    char token[64];
    const char *cursor = original_line;
    uint32_t out_len = 0;
    uint32_t i;

    if (!ends_with_ignore_case_local(path, ".wav")) {
        return 0;
    }
    copy_line_local(out, "/cmd/mplay", out_size);
    out_len = str_len_local(out);
    if (out_len + str_len_local(path) + 2u >= out_size) {
        return 0;
    }
    out[out_len++] = ' ';
    for (i = 0; path[i] != '\0'; i++) {
        out[out_len++] = path[i];
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
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

static int ush_try_wav_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_wav_command(line, path, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_ansi_error, "mplay exec failed: ");
    ush_write_colored_err(ush_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}

static const char *ush_exit_reason_local(int32_t exit_code) {
    switch (exit_code) {
        case 130:
            return "Interrupted";
        case -4:
            return "Illegal instruction";
        case -8:
            return "Floating point exception";
        case -11:
            return "Segmentation fault";
        default:
            return NULL;
    }
}

static void ush_report_foreground_exit_status(void) {
    static uint32_t last_reported_pid = 0;
    struct syscall_process_info info;
    const char *reason;

    if (proc_query(NEX_PROC_QUERY_LAST_EXIT, 0, &info) <= 0) {
        return;
    }
    reason = ush_exit_reason_local(info.exit_code);
    if (reason == NULL) {
        return;
    }
    if (info.pid == last_reported_pid) {
        return;
    }
    last_reported_pid = info.pid;
    write_err_str(reason);
    write_err_str("\n");
}

static int ush_last_foreground_status_local(void) {
    struct syscall_process_info info;

    if (proc_query(NEX_PROC_QUERY_LAST_EXIT, 0, &info) <= 0) {
        return 0;
    }
    return info.exit_code == 0 ? 0 : 1;
}

static void ush_capture_process_ids_local(uint32_t *out) {
    struct syscall_process_info info;
    uint32_t i;

    if (out == NULL) {
        return;
    }
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        out[i] = 0u;
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0) {
            out[i] = info.pid;
        }
    }
}

static int ush_pid_seen_local(const uint32_t *snapshot, uint32_t pid) {
    uint32_t i;

    if (snapshot == NULL) {
        return 0;
    }
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t ush_find_new_process_pid_local(const uint32_t *snapshot) {
    struct syscall_process_info info;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid != 0u && !ush_pid_seen_local(snapshot, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static void ush_report_background_start_local(uint32_t pid, const char *command) {
    if (pid != 0u) {
        printf("[bg] pid=%u ", pid);
    } else {
        write_str("[bg] pid=? ");
    }
    write_str(command);
    write_str("\n");
}

static int ush_spawn_command_local(const char *command, uint32_t mode, int background) {
    uint32_t before[NEX_PROC_SLOTS_MAX];
    uint32_t pid = 0u;
    int rc;

    if (background) {
        ush_capture_process_ids_local(before);
    }
    rc = spawn(command, mode, background ? SYS_SPAWN_BACKGROUND : 0);
    if (rc != 0) {
        return rc;
    }
    if (background) {
        pid = ush_find_new_process_pid_local(before);
        ush_report_background_start_local(pid, command);
    } else {
        ush_report_foreground_exit_status();
    }
    return 0;
}

static void ush_report_exec_load_failure_local(const char *command, int rc) {
    ush_write_colored_err(ush_ansi_error, "exec failed: ");
    ush_write_colored_err(ush_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
}

static int ush_try_external_command(char *cwd, const char *line, int background, int *handled_out) {
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
        ush_write_colored_err(ush_ansi_error, "exec failed: ");
        ush_write_colored_err(ush_ansi_value, command);
        write_err_str(" rc=");
        eprintf("%d\n", rc);
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 1;
    }

    (void)cwd;
    if (ush_build_cmd_search_command_from(line, "/ram/CMD", 0, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        if (rc < 0 && rc != -2) {
            ush_report_exec_load_failure_local(search_command, rc);
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 1;
        }
    }

    if (ush_build_cmd_search_command_lower(line, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        if (rc < 0 && rc != -2) {
            ush_report_exec_load_failure_local(search_command, rc);
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 1;
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
        if (rc < 0 && rc != -2) {
            ush_report_exec_load_failure_local(search_command, rc);
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 1;
        }
    }

    if (background) {
        rc = ush_spawn_command_local(line, SYS_SPAWN_AUTO, 1);
    } else {
        rc = exec(line);
    }
    if (rc == 0) {
        if (!background) {
            ush_report_foreground_exit_status();
        }
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return background ? 0 : ush_last_foreground_status_local();
    }
    if (ush_try_wav_command(line, token, background)) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 0;
    }

    if (handled_out != NULL) {
        *handled_out = 0;
    }
    return 0;
}

static int ush_change_directory(char *cwd, uint32_t cwd_size, const char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        if (chdir("/") != 0) {
            ush_write_error("cd: no such directory\n");
            return 1;
        }
        ush_refresh_cwd_local(cwd, cwd_size);
        return 0;
    }
    if (chdir(arg) != 0) {
        ush_write_error("cd: no such directory\n");
        return 1;
    }
    ush_refresh_cwd_local(cwd, cwd_size);
    return 0;
}

static int ush_open_resolved_path(const char *cwd, const char *arg, uint32_t flags) {
    (void)cwd;
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }
    return open(arg, flags);
}

static void ush_init_saved_stdio(uint32_t saved[3]) {
    saved[0] = 0xffffffffu;
    saved[1] = 0xffffffffu;
    saved[2] = 0xffffffffu;
}

static void ush_restore_stdio(const uint32_t saved[3]);

static int ush_save_one_stdio(uint32_t saved[3], uint32_t fd_index) {
    static const uint32_t save_fds[3] = {13u, 14u, 15u};
    uint32_t save_fd;

    if (saved == NULL || fd_index > 2u || saved[fd_index] != 0xffffffffu) {
        return 0;
    }
    save_fd = save_fds[fd_index];
    if (dup2(fd_index, save_fd) < 0) {
        return 0;
    }
    saved[fd_index] = save_fd;
    return 1;
}

static int ush_save_stdio(uint32_t saved[3], int save_stdin, int save_stdout, int save_stderr) {
    ush_init_saved_stdio(saved);
    if (save_stdin && !ush_save_one_stdio(saved, 0u)) {
        return 0;
    }
    if (save_stdout && !ush_save_one_stdio(saved, 1u)) {
        ush_restore_stdio(saved);
        return 0;
    }
    if (save_stderr && !ush_save_one_stdio(saved, 2u)) {
        ush_restore_stdio(saved);
        return 0;
    }
    return 1;
}

static void ush_restore_stdio(const uint32_t saved[3]) {
    if (saved[0] != 0xffffffffu) {
        (void)dup2(saved[0], STDIN_FILENO);
        if (saved[0] != STDIN_FILENO) {
            (void)close(saved[0]);
        }
    }
    if (saved[1] != 0xffffffffu) {
        (void)dup2(saved[1], STDOUT_FILENO);
        if (saved[1] != STDOUT_FILENO) {
            (void)close(saved[1]);
        }
    }
    if (saved[2] != 0xffffffffu) {
        (void)dup2(saved[2], STDERR_FILENO);
        if (saved[2] != STDERR_FILENO) {
            (void)close(saved[2]);
        }
    }
}

static void ush_close_if_not_target_local(int fd, uint32_t target_fd) {
    if (fd >= 0 && (uint32_t)fd != target_fd) {
        close((uint32_t)fd);
    }
}

static int ush_split_pipeline_stages_local(const char *line,
                                           char stage_texts[][USH_LINE_MAX + 1],
                                           uint32_t stage_max,
                                           uint32_t *stage_count_out) {
    uint32_t pos = 0;
    uint32_t stage_index = 0;
    uint32_t stage_len = 0;
    int found = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL || stage_texts == NULL || stage_max == 0 || stage_count_out == NULL) {
        return -1;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            if (stage_len + 2u >= USH_LINE_MAX + 1u) {
                return -1;
            }
            stage_texts[stage_index][stage_len++] = line[pos++];
            stage_texts[stage_index][stage_len++] = line[pos++];
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        }
        if (!single_quote && !double_quote && line[pos] == '|') {
            if (stage_index + 1u >= stage_max) {
                return -1;
            }
            stage_texts[stage_index][stage_len] = '\0';
            trim_in_place_local(stage_texts[stage_index]);
            if (stage_texts[stage_index][0] == '\0') {
                return -1;
            }
            stage_index++;
            stage_len = 0;
            found = 1;
            pos++;
            continue;
        }
        if (stage_len + 1u >= USH_LINE_MAX + 1u) {
            return -1;
        }
        stage_texts[stage_index][stage_len++] = line[pos++];
    }
    stage_texts[stage_index][stage_len] = '\0';
    trim_in_place_local(stage_texts[stage_index]);
    if (stage_texts[stage_index][0] == '\0') {
        return -1;
    }
    *stage_count_out = stage_index + 1u;
    return found ? 1 : 0;
}

static int ush_validate_pipeline_local(const struct ush_command_spec *stages, uint32_t stage_count) {
    uint32_t i;

    if (stages == NULL || stage_count < 2u) {
        return 0;
    }
    for (i = 0; i < stage_count; i++) {
        if (i != 0u && stages[i].input[0] != '\0') {
            write_err_str("pipe: only the first command can use input redirection\n");
            return 0;
        }
        if (i + 1u != stage_count && stages[i].output[0] != '\0') {
            write_err_str("pipe: only the last command can use output redirection\n");
            return 0;
        }
    }
    return 1;
}

static int ush_configure_pipeline_stdio(const uint32_t saved[3],
                                        int read_fd,
                                        int write_fd,
                                        int restore_stderr) {
    if (read_fd >= 0) {
        if (dup2(read_fd, STDIN_FILENO) < 0) {
            return 0;
        }
    } else if (saved[0] != 0xffffffffu && dup2(saved[0], STDIN_FILENO) < 0) {
        return 0;
    }
    if (write_fd >= 0) {
        if (dup2(write_fd, STDOUT_FILENO) < 0) {
            return 0;
        }
    } else if (saved[1] != 0xffffffffu && dup2(saved[1], STDOUT_FILENO) < 0) {
        return 0;
    }
    if (restore_stderr && saved[2] != 0xffffffffu && dup2(saved[2], STDERR_FILENO) < 0) {
        return 0;
    }
    return 1;
}

static int ush_apply_pipeline_stage_redirections(const char *cwd,
                                                 const struct ush_command_spec *spec,
                                                 int allow_input,
                                                 int allow_output) {
    int fd;

    if (spec->input[0] != '\0') {
        if (!allow_input) {
            write_err_str("pipe: only the first command can use input redirection\n");
            return 0;
        }
        if (!ush_check_device_redirect_cap(cwd, spec->input, USH_ACTION_CAP_DEVICE_READ, "device.read")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->input, 0);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDIN_FILENO);
            write_err_str("pipe: input open failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDIN_FILENO);
    }
    if (spec->output[0] != '\0') {
        uint32_t flags;

        if (!allow_output) {
            write_err_str("pipe: only the last command can use output redirection\n");
            return 0;
        }
        if (!ush_check_device_redirect_cap(cwd, spec->output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        flags = O_CREAT | (spec->append ? O_APPEND : O_TRUNC);
        fd = ush_open_resolved_path(cwd, spec->output, flags);
        if (fd < 0) {
            write_err_str("pipe: output open failed\n");
            return 0;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDOUT_FILENO);
            write_err_str("pipe: output dup failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDOUT_FILENO);
    }
    if (spec->stderr_to_stdout) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            write_err_str("pipe: stderr dup failed\n");
            return 0;
        }
    } else if (spec->err_output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->err_append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->err_output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->err_output, flags);
        if (fd < 0) {
            write_err_str("pipe: stderr open failed\n");
            return 0;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDERR_FILENO);
            write_err_str("pipe: stderr dup failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDERR_FILENO);
    }
    return 1;
}

static int ush_apply_redirections(const char *cwd, const struct ush_command_spec *spec) {
    int fd;

    if (spec->input[0] != '\0') {
        if (!ush_check_device_redirect_cap(cwd, spec->input, USH_ACTION_CAP_DEVICE_READ, "device.read")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->input, 0);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDIN_FILENO);
            write_err_str("redirect: input open failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDIN_FILENO);
    }
    if (spec->output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->output, flags);
        if (fd < 0) {
            write_err_str("redirect: output open failed: ");
            write_err_str(spec->output);
            write_err_str("\n");
            return 0;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDOUT_FILENO);
            write_err_str("redirect: output dup failed: ");
            write_err_str(spec->output);
            write_err_str("\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDOUT_FILENO);
    }
    if (spec->stderr_to_stdout) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            write_err_str("redirect: stderr dup failed\n");
            return 0;
        }
    } else if (spec->err_output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->err_append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->err_output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->err_output, flags);
        if (fd < 0) {
            write_err_str("redirect: stderr open failed: ");
            write_err_str(spec->err_output);
            write_err_str("\n");
            return 0;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDERR_FILENO);
            write_err_str("redirect: stderr dup failed: ");
            write_err_str(spec->err_output);
            write_err_str("\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDERR_FILENO);
    }
    return 1;
}

static int ush_execute_command_core(char *cwd, const char *line, int background) {
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
            ush_write_colored_err(ush_ansi_error, "exec failed: ");
            ush_write_colored_err(ush_ansi_value, line + 5);
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
    ush_write_colored_err(ush_ansi_error, "unknown command: ");
    ush_write_colored_err(ush_ansi_value, line);
    write_err_str("\n");
    return 1;
}

static int ush_execute_with_redirection(char *cwd, const struct ush_command_spec *spec, int background) {
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

static int ush_execute_pipeline(char *cwd,
                                const struct ush_command_spec *stages,
                                uint32_t stage_count) {
    uint32_t saved[3];
    int prev_read = -1;
    int next_read = -1;
    int next_write = -1;
    int rc = 0;
    uint32_t i;

    if (stages == NULL || stage_count < 2u) {
        return 1;
    }
    if (!ush_validate_pipeline_local(stages, stage_count)) {
        return 1;
    }
    if (!ush_save_stdio(saved, 1, 1, 1)) {
        write_err_str("pipe: stdio save failed\n");
        return 1;
    }
    for (i = 0; i < stage_count; i++) {
        next_read = -1;
        next_write = -1;
        if (i + 1u < stage_count) {
            int pair[2];

            if (pipe(pair) < 0) {
                write_err_str("pipe failed\n");
                goto cleanup_pipe;
            }
            next_read = pair[0];
            next_write = pair[1];
        }
        if (!ush_configure_pipeline_stdio(saved, prev_read, next_write, 1)) {
            write_err_str("pipe: dup2 failed\n");
            goto cleanup_pipe;
        }
        if (!ush_apply_pipeline_stage_redirections(cwd,
                                                   &stages[i],
                                                   i == 0u,
                                                   i + 1u == stage_count)) {
            goto cleanup_pipe;
        }
        rc = ush_execute_command_core(cwd, stages[i].command, 0);
        if (next_write >= 0) {
            close((uint32_t)next_write);
            next_write = -1;
        }
        if (prev_read >= 0) {
            close((uint32_t)prev_read);
            prev_read = -1;
        }
        prev_read = next_read;
        next_read = -1;
        if (rc < 0) {
            goto cleanup_pipe;
        }
    }
    ush_restore_stdio(saved);
    if (prev_read >= 0) {
        close((uint32_t)prev_read);
    }
    return rc;

cleanup_pipe:
    if (next_write >= 0) {
        close((uint32_t)next_write);
    }
    if (next_read >= 0) {
        close((uint32_t)next_read);
    }
    if (prev_read >= 0) {
        close((uint32_t)prev_read);
    }
    ush_restore_stdio(saved);
    return rc < 0 ? rc : 1;
}

static int ush_execute_line_core(char *cwd, const char *line) {
    char seq_left[USH_LINE_MAX + 1];
    char seq_right[USH_LINE_MAX + 1];
    char bg_left[USH_LINE_MAX + 1];
    char bg_right[USH_LINE_MAX + 1];
    char bg_command[USH_LINE_MAX + 1];
    char or_left[USH_LINE_MAX + 1];
    char or_right[USH_LINE_MAX + 1];
    char and_left[USH_LINE_MAX + 1];
    char and_right[USH_LINE_MAX + 1];
    char background_line[USH_LINE_MAX + 1];
    char expanded_line[USH_LINE_MAX + 1];
    char expanded_command[USH_LINE_MAX + 1];
    char pipeline_texts[USH_PIPELINE_STAGE_MAX][USH_LINE_MAX + 1];
    struct ush_command_spec pipeline_stages[USH_PIPELINE_STAGE_MAX];
    uint32_t pipeline_stage_count = 0;
    uint32_t i;
    int seq_rc;
    int bg_list_rc;
    int or_rc;
    int and_rc;
    int background = 0;
    int background_rc;
    int pipeline_rc;

    seq_rc = ush_split_sequence(line, seq_left, sizeof(seq_left), seq_right, sizeof(seq_right));
    if (seq_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (seq_rc > 0) {
        (void)ush_execute_line(cwd, seq_left);
        return ush_execute_line(cwd, seq_right);
    }
    bg_list_rc = ush_split_background_list(line, bg_left, sizeof(bg_left), bg_right, sizeof(bg_right));
    if (bg_list_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (bg_list_rc > 0) {
        uint32_t bg_len = str_len_local(bg_left);

        if (bg_len + 3u > sizeof(bg_command)) {
            write_err_str("parse error\n");
            return 1;
        }
        copy_line_local(bg_command, bg_left, sizeof(bg_command));
        bg_command[bg_len++] = ' ';
        bg_command[bg_len++] = '&';
        bg_command[bg_len] = '\0';
        (void)ush_execute_line(cwd, bg_command);
        return ush_execute_line(cwd, bg_right);
    }
    or_rc = ush_split_orif(line, or_left, sizeof(or_left), or_right, sizeof(or_right));
    if (or_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (or_rc > 0) {
        int left_status = ush_execute_line(cwd, or_left);

        if (left_status == 0) {
            return 0;
        }
        return ush_execute_line(cwd, or_right);
    }
    and_rc = ush_split_andif(line, and_left, sizeof(and_left), and_right, sizeof(and_right));
    if (and_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (and_rc > 0) {
        int left_status = ush_execute_line(cwd, and_left);

        if (left_status != 0) {
            return left_status;
        }
        return ush_execute_line(cwd, and_right);
    }
    background_rc = ush_strip_trailing_background_local(line,
                                                       background_line,
                                                       sizeof(background_line),
                                                       &background);
    if (background_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (!ush_expand_variables_local(background_line, expanded_line, sizeof(expanded_line))) {
        write_err_str("expand error\n");
        return 1;
    }
    pipeline_rc = ush_split_pipeline_stages_local(expanded_line,
                                                  pipeline_texts,
                                                  USH_PIPELINE_STAGE_MAX,
                                                  &pipeline_stage_count);
    if (pipeline_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    for (i = 0; i < pipeline_stage_count; i++) {
        if (!ush_parse_command_spec(pipeline_texts[i], &pipeline_stages[i])) {
            write_err_str("parse error\n");
            return 1;
        }
        if (!ush_expand_command_text_local(pipeline_stages[i].command,
                                           expanded_command,
                                           sizeof(expanded_command))) {
            write_err_str("expand error\n");
            return 1;
        }
        copy_line_local(pipeline_stages[i].command,
                        expanded_command,
                        sizeof(pipeline_stages[i].command));
    }
    if (pipeline_stage_count == 1u) {
        return ush_execute_with_redirection(cwd, &pipeline_stages[0], background);
    }
    if (background) {
        write_err_str("background: pipelines are not supported\n");
        return 1;
    }
    return ush_execute_pipeline(cwd, pipeline_stages, pipeline_stage_count);
}

int ush_execute_line(char *cwd, const char *line) {
    int function_handled = 0;
    int function_rc = ush_try_function_call_local(cwd, line, 1, &function_handled);

    if (function_handled) {
        return function_rc;
    }
    return ush_execute_line_core(cwd, line);
}
