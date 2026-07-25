#include "user/apps/elf/ush_exec_internal.h"

static const char ush_ansi_reset[] = "\x1b[0m";

enum {
    USH_FUNCTION_CALL_DEPTH_MAX = 4u,
    USH_ACTION_POLICY_FILE_MAX = 768u
};

static uint32_t g_ush_function_call_depth;
static int g_ush_function_recursion_limit_enabled = 1;
int g_ush_suppress_background_report;
uint32_t g_ush_last_background_pid;
int g_ush_last_foreground_status;
static const char *g_ush_action_caps_path = "/HOME/ACTION.CAPS";

struct ush_execute_workspace {
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
};

static struct ush_execute_workspace g_ush_execute_workspace;

int streq_local(const char *a, const char *b) {
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

int starts_with_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

int contains_char_local(const char *text, char ch) {
    uint32_t i = 0;

    while (text[i] != '\0') {
        if (text[i] == ch) {
            return 1;
        }
        i++;
    }
    return 0;
}

uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

void copy_line_local(char *dst, const char *src, uint32_t max_len) {
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

int ush_check_device_redirect_cap(const char *cwd, const char *path, uint32_t cap, const char *op) {
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

void ush_write_colored_err(const char *ansi, const char *text) {
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

void upper_in_place_local(char *text) {
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

void lower_in_place_local(char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    while (text[i] != '\0') {
        text[i] = to_lower_ascii_local(text[i]);
        i++;
    }
}

int ends_with_ignore_case_local(const char *text, const char *suffix) {
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

const char *skip_spaces_local(const char *text) {
    while (text != NULL && is_space_local(*text)) {
        text++;
    }
    return text;
}

int read_token_local(const char **text_io, char *out, uint32_t out_size);

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

int ush_try_function_call_local(char *cwd, const char *line, int require_plain_line, int *handled_out) {
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

void trim_in_place_local(char *text) {
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

    if (line == NULL ||
        line[pos] != '&' ||
        line[pos + 1u] == '&' ||
        (pos > 0u && line[pos - 1u] == '&')) {
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

int read_token_local(const char **text_io, char *out, uint32_t out_size) {
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

int ush_parse_exit_code_local(const char *text, uint64_t *code_out) {
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

int ush_change_directory(char *cwd, uint32_t cwd_size, const char *arg) {
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

int ush_open_resolved_path(const char *cwd, const char *arg, uint32_t flags) {
    (void)cwd;
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }
    return open(arg, flags);
}

int ush_preload_file_local(const char *path) {
    static uint8_t buffer[16384];
    int fd;

    if (path == NULL || path[0] == '\0') {
        write_err_str("usage: preload <file>\n");
        return 1;
    }
    fd = open(path, 0);
    if (fd < 0) {
        write_err_str("preload: open failed: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    for (;;) {
        ssize_t got = read((uint32_t)fd, buffer, sizeof(buffer));

        if (got < 0) {
            close((uint32_t)fd);
            write_err_str("preload: read failed: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        if (got == 0) {
            break;
        }
    }
    close((uint32_t)fd);
    return 0;
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

int ush_execute_pipeline_stage_command(char *cwd, const char *line, uint32_t *pid_out) {
    int previous_suppress = g_ush_suppress_background_report;
    int rc;

    if (pid_out != NULL) {
        *pid_out = 0u;
    }
    g_ush_suppress_background_report = 1;
    rc = ush_execute_command_core(cwd, line, 1);
    g_ush_suppress_background_report = previous_suppress;
    if (rc == 0 && pid_out != NULL) {
        *pid_out = g_ush_last_background_pid;
    }
    return rc;
}

static int ush_execute_line_core(char *cwd, const char *line) {
    struct ush_execute_workspace *workspace = &g_ush_execute_workspace;
    uint32_t pipeline_stage_count = 0;
    uint32_t i;
    int seq_rc;
    int bg_list_rc;
    int or_rc;
    int and_rc;
    int background = 0;
    int background_rc;
    int pipeline_rc;

    seq_rc = ush_split_sequence(line,
                                workspace->seq_left,
                                sizeof(workspace->seq_left),
                                workspace->seq_right,
                                sizeof(workspace->seq_right));
    if (seq_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (seq_rc > 0) {
        (void)ush_execute_line(cwd, workspace->seq_left);
        return ush_execute_line(cwd, workspace->seq_right);
    }
    bg_list_rc = ush_split_background_list(line,
                                           workspace->bg_left,
                                           sizeof(workspace->bg_left),
                                           workspace->bg_right,
                                           sizeof(workspace->bg_right));
    if (bg_list_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (bg_list_rc > 0) {
        uint32_t bg_len = str_len_local(workspace->bg_left);

        if (bg_len + 3u > sizeof(workspace->bg_command)) {
            write_err_str("parse error\n");
            return 1;
        }
        copy_line_local(workspace->bg_command,
                        workspace->bg_left,
                        sizeof(workspace->bg_command));
        workspace->bg_command[bg_len++] = ' ';
        workspace->bg_command[bg_len++] = '&';
        workspace->bg_command[bg_len] = '\0';
        (void)ush_execute_line(cwd, workspace->bg_command);
        return ush_execute_line(cwd, workspace->bg_right);
    }
    or_rc = ush_split_orif(line,
                           workspace->or_left,
                           sizeof(workspace->or_left),
                           workspace->or_right,
                           sizeof(workspace->or_right));
    if (or_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (or_rc > 0) {
        int left_status = ush_execute_line(cwd, workspace->or_left);

        if (left_status == 0) {
            return 0;
        }
        return ush_execute_line(cwd, workspace->or_right);
    }
    and_rc = ush_split_andif(line,
                             workspace->and_left,
                             sizeof(workspace->and_left),
                             workspace->and_right,
                             sizeof(workspace->and_right));
    if (and_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (and_rc > 0) {
        int left_status = ush_execute_line(cwd, workspace->and_left);

        if (left_status != 0) {
            return left_status;
        }
        return ush_execute_line(cwd, workspace->and_right);
    }
    background_rc = ush_strip_trailing_background_local(line,
                                                       workspace->background_line,
                                                       sizeof(workspace->background_line),
                                                       &background);
    if (background_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (!ush_expand_variables_local(workspace->background_line,
                                    workspace->expanded_line,
                                    sizeof(workspace->expanded_line))) {
        write_err_str("expand error\n");
        return 1;
    }
    pipeline_rc = ush_split_pipeline_stages_local(workspace->expanded_line,
                                                  workspace->pipeline_texts,
                                                  USH_PIPELINE_STAGE_MAX,
                                                  &pipeline_stage_count);
    if (pipeline_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    for (i = 0; i < pipeline_stage_count; i++) {
        if (!ush_parse_command_spec(workspace->pipeline_texts[i],
                                    &workspace->pipeline_stages[i])) {
            write_err_str("parse error\n");
            return 1;
        }
        if (!ush_expand_command_text_local(workspace->pipeline_stages[i].command,
                                           workspace->expanded_command,
                                           sizeof(workspace->expanded_command))) {
            write_err_str("expand error\n");
            return 1;
        }
        copy_line_local(workspace->pipeline_stages[i].command,
                        workspace->expanded_command,
                        sizeof(workspace->pipeline_stages[i].command));
    }
    if (pipeline_stage_count == 1u) {
        return ush_execute_with_redirection(cwd,
                                            &workspace->pipeline_stages[0],
                                            background);
    }
    if (background) {
        write_err_str("background: pipelines are not supported\n");
        return 1;
    }
    return ush_execute_pipeline(cwd,
                                workspace->pipeline_stages,
                                pipeline_stage_count);
}

int ush_execute_line(char *cwd, const char *line) {
    int function_handled = 0;
    int function_rc;

    function_rc = ush_try_function_call_local(cwd, line, 1, &function_handled);
    if (function_handled) {
        return function_rc;
    }
    return ush_execute_line_core(cwd, line);
}
