#include "user/apps/elf/ush/ush_exec_internal.h"

static int ush_parse_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
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
    while (prev > 0u && ush_parse_is_space(line[prev - 1u])) {
        prev--;
    }
    if (prev > 0u && line[prev - 1u] == '>') {
        return 0;
    }
    return 1;
}

enum ush_exec_split_kind {
    USH_EXEC_SPLIT_SEQUENCE,
    USH_EXEC_SPLIT_ANDIF,
    USH_EXEC_SPLIT_ORIF,
    USH_EXEC_SPLIT_BACKGROUND_LIST
};

static uint32_t ush_split_delimiter_len(const char *line,
                                        uint32_t pos,
                                        enum ush_exec_split_kind kind) {
    switch (kind) {
        case USH_EXEC_SPLIT_SEQUENCE:
            return line[pos] == ';' ? 1u : 0u;
        case USH_EXEC_SPLIT_ANDIF:
            return line[pos] == '&' && line[pos + 1u] == '&' ? 2u : 0u;
        case USH_EXEC_SPLIT_ORIF:
            return line[pos] == '|' && line[pos + 1u] == '|' ? 2u : 0u;
        case USH_EXEC_SPLIT_BACKGROUND_LIST:
            return ush_is_background_amp_local(line, pos) ? 1u : 0u;
        default:
            return 0u;
    }
}

static int ush_split_binary_operator(const char *line,
                                     char *left,
                                     uint32_t left_size,
                                     char *right,
                                     uint32_t right_size,
                                     enum ush_exec_split_kind kind) {
    uint32_t pos = 0;
    uint32_t left_len = 0;
    uint32_t right_len = 0;
    uint32_t delimiter_len;
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
        delimiter_len = (!found && !single_quote && !double_quote) ?
            ush_split_delimiter_len(line, pos, kind) : 0u;
        if (delimiter_len != 0u) {
            found = 1;
            pos += delimiter_len;
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
    if (kind == USH_EXEC_SPLIT_BACKGROUND_LIST && (single_quote || double_quote)) {
        return -1;
    }
    if (!found) {
        right[0] = '\0';
        return 0;
    }
    if (left[0] == '\0') {
        return -1;
    }
    if (kind == USH_EXEC_SPLIT_BACKGROUND_LIST) {
        return right[0] == '\0' ? 0 : 1;
    }
    if (right[0] == '\0') {
        return -1;
    }
    return 1;
}

int ush_split_andif(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    return ush_split_binary_operator(line, left, left_size, right, right_size, USH_EXEC_SPLIT_ANDIF);
}

int ush_split_orif(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    return ush_split_binary_operator(line, left, left_size, right, right_size, USH_EXEC_SPLIT_ORIF);
}

int ush_split_sequence(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    return ush_split_binary_operator(line, left, left_size, right, right_size, USH_EXEC_SPLIT_SEQUENCE);
}

int ush_split_background_list(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size) {
    return ush_split_binary_operator(line, left, left_size, right, right_size, USH_EXEC_SPLIT_BACKGROUND_LIST);
}

int ush_strip_trailing_background_local(const char *line,
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
            while (ush_parse_is_space(line[pos])) {
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

int ush_split_pipeline_stages_local(const char *line,
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
