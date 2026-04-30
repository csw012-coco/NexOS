#include "user/apps/elf/ush_shared.h"

static uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text != NULL && text[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_line_local(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (dst == NULL || max_len == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < max_len) {
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

static int append_raw_token_local(char *dst,
                                  uint32_t dst_size,
                                  const char *text,
                                  uint32_t start,
                                  uint32_t end) {
    uint32_t len = str_len_local(dst);
    uint32_t i;

    if (end < start) {
        return 0;
    }
    if (len + (end - start) + 1u >= dst_size) {
        return 0;
    }
    if (len != 0) {
        dst[len++] = ' ';
    }
    for (i = start; i < end; i++) {
        dst[len++] = text[i];
    }
    dst[len] = '\0';
    return 1;
}

static int parse_token_local(const char *text,
                             uint32_t *pos_io,
                             char *token,
                             uint32_t token_size,
                             uint32_t *raw_start_out,
                             uint32_t *raw_end_out,
                             int *quoted_out) {
    uint32_t pos;
    uint32_t out_len = 0;
    int single_quote = 0;
    int double_quote = 0;
    int quoted = 0;

    if (text == NULL || pos_io == NULL || token == NULL || token_size == 0) {
        return 0;
    }
    pos = *pos_io;
    while (is_space_local(text[pos])) {
        pos++;
    }
    if (text[pos] == '\0') {
        token[0] = '\0';
        *pos_io = pos;
        return 0;
    }
    if (raw_start_out != NULL) {
        *raw_start_out = pos;
    }
    while (text[pos] != '\0') {
        char ch = text[pos];

        if (!single_quote && ch == '\\') {
            pos++;
            if (text[pos] == '\0') {
                break;
            }
            ch = text[pos++];
        } else if (!double_quote && ch == '\'') {
            quoted = 1;
            single_quote = !single_quote;
            pos++;
            continue;
        } else if (!single_quote && ch == '"') {
            quoted = 1;
            double_quote = !double_quote;
            pos++;
            continue;
        } else if (!single_quote && !double_quote &&
                   (is_space_local(ch) || ch == '<' || ch == '>' || ch == '|')) {
            break;
        } else {
            pos++;
        }

        if (out_len + 1u >= token_size) {
            return 0;
        }
        token[out_len++] = ch;
    }
    if (single_quote || double_quote) {
        return 0;
    }
    token[out_len] = '\0';
    if (raw_end_out != NULL) {
        *raw_end_out = pos;
    }
    if (quoted_out != NULL) {
        *quoted_out = quoted;
    }
    *pos_io = pos;
    return out_len != 0;
}

static void ush_command_spec_reset(struct ush_command_spec *spec) {
    if (spec == NULL) {
        return;
    }
    spec->command[0] = '\0';
    spec->input[0] = '\0';
    spec->output[0] = '\0';
    spec->err_output[0] = '\0';
    spec->append = 0;
    spec->err_append = 0;
    spec->stderr_to_stdout = 0;
}

int ush_parse_command_spec(const char *text, struct ush_command_spec *spec) {
    uint32_t pos = 0;

    if (text == NULL || spec == NULL) {
        return 0;
    }
    ush_command_spec_reset(spec);
    while (text[pos] != '\0') {
        char token[64];
        uint32_t raw_start = 0;
        uint32_t raw_end = 0;
        char *target = spec->command;
        uint32_t target_size = sizeof(spec->command);
        int target_is_command = 1;
        int quoted = 0;

        while (is_space_local(text[pos])) {
            pos++;
        }
        if (text[pos] == '\0') {
            break;
        }
        if (text[pos] == '<') {
            target = spec->input;
            target_size = sizeof(spec->input);
            target_is_command = 0;
            pos++;
        } else if (text[pos] == '2' && text[pos + 1u] == '>' && text[pos + 2u] == '&' && text[pos + 3u] == '1') {
            spec->stderr_to_stdout = 1u;
            spec->err_output[0] = '\0';
            spec->err_append = 0;
            pos += 4u;
            continue;
        } else if (text[pos] == '2' && text[pos + 1u] == '>') {
            target = spec->err_output;
            target_size = sizeof(spec->err_output);
            target_is_command = 0;
            pos += 2u;
            if (text[pos] == '>') {
                spec->err_append = 1u;
                pos++;
            } else {
                spec->err_append = 0;
            }
        } else if (text[pos] == '1' && text[pos + 1u] == '>') {
            target = spec->output;
            target_size = sizeof(spec->output);
            target_is_command = 0;
            pos += 2u;
            if (text[pos] == '>') {
                spec->append = 1u;
                pos++;
            } else {
                spec->append = 0;
            }
        } else if (text[pos] == '>') {
            target = spec->output;
            target_size = sizeof(spec->output);
            target_is_command = 0;
            pos++;
            if (text[pos] == '>') {
                spec->append = 1u;
                pos++;
            } else {
                spec->append = 0;
            }
        }
        while (is_space_local(text[pos])) {
            pos++;
        }
        if (text[pos] == '\0') {
            return 0;
        }
        if (!parse_token_local(text,
                               &pos,
                               token,
                               sizeof(token),
                               &raw_start,
                               &raw_end,
                               &quoted)) {
            return 0;
        }
        (void)quoted;
        if (target_is_command) {
            if (!append_raw_token_local(target, target_size, text, raw_start, raw_end)) {
                return 0;
            }
        } else {
            copy_line_local(target, token, target_size);
        }
    }
    trim_in_place_local(spec->command);
    return spec->command[0] != '\0';
}

int ush_split_pipeline(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
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
        if (!single_quote && !double_quote && line[pos] == '|') {
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
