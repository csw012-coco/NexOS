#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

#define ED_LINE_MAX 128u
#define ED_BUFFER_LINES 256u

struct ed_buffer {
    char path[CMD_PATH_MAX];
    char lines[ED_BUFFER_LINES][ED_LINE_MAX];
    uint32_t count;
    uint32_t current;
    uint8_t dirty;
};

struct ed_address_range {
    int has_any;
    int has_comma;
    int start_present;
    int end_present;
    int32_t start;
    int32_t end;
};

static struct ed_buffer g_ed_buffer;

static const char *ed_skip_spaces_local(const char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) {
        text++;
    }
    return text;
}

static void ed_print_line_local(const struct ed_buffer *buffer, uint32_t index, int numbered) {
    if (buffer == NULL || index >= buffer->count) {
        return;
    }
    if (numbered) {
        printf("%u\t%s\n", index + 1u, buffer->lines[index]);
        return;
    }
    write_str(buffer->lines[index]);
    write_str("\n");
}

static void ed_print_range_local(const struct ed_buffer *buffer, uint32_t start, uint32_t end, int numbered) {
    uint32_t i;

    if (buffer == NULL || buffer->count == 0u || start == 0u || end == 0u || start > end) {
        return;
    }
    for (i = start; i <= end; i++) {
        ed_print_line_local(buffer, i - 1u, numbered);
    }
}

static int ed_insert_slot_local(struct ed_buffer *buffer, uint32_t index, const char *line) {
    uint32_t i;

    if (buffer == NULL || line == NULL || index > buffer->count || buffer->count >= ED_BUFFER_LINES) {
        return 0;
    }
    for (i = buffer->count; i > index; i--) {
        copy_line_local(buffer->lines[i], buffer->lines[i - 1u], sizeof(buffer->lines[i]));
    }
    copy_line_local(buffer->lines[index], line, sizeof(buffer->lines[index]));
    buffer->count++;
    buffer->current = index + 1u;
    buffer->dirty = 1u;
    return 1;
}

static int ed_load_file_local(struct ed_buffer *buffer, const char *path) {
    char line[ED_LINE_MAX];
    int fd;

    if (buffer == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }
    fd = cmd_open_resolved_path(path, 0);
    if (fd < 0) {
        return 0;
    }
    while (buffer->count < ED_BUFFER_LINES && read_line((uint32_t)fd, line, sizeof(line)) > 0) {
        copy_line_local(buffer->lines[buffer->count], line, sizeof(buffer->lines[buffer->count]));
        buffer->count++;
    }
    close((uint32_t)fd);
    copy_line_local(buffer->path, path, sizeof(buffer->path));
    buffer->current = buffer->count != 0u ? buffer->count : 0u;
    buffer->dirty = 0u;
    return 1;
}

static int ed_write_file_local(struct ed_buffer *buffer, const char *path) {
    uint32_t i;
    uint32_t written = 0u;
    int fd;

    if (buffer == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        return 0;
    }
    for (i = 0; i < buffer->count; i++) {
        uint32_t len = str_len_local(buffer->lines[i]);

        if (len != 0u && write_fd((uint32_t)fd, buffer->lines[i], len) != len) {
            close((uint32_t)fd);
            return 0;
        }
        written += len;
        if (write_fd((uint32_t)fd, "\n", 1u) != 1u) {
            close((uint32_t)fd);
            return 0;
        }
        written++;
    }
    close((uint32_t)fd);
    copy_line_local(buffer->path, path, sizeof(buffer->path));
    buffer->dirty = 0u;
    printf("%u\n", written);
    return 1;
}

static int ed_input_mode_local(struct ed_buffer *buffer, uint32_t insert_at) {
    char line[ED_LINE_MAX];

    for (;;) {
        write_str("! ");
        if (read_line(STDIN_FILENO, line, sizeof(line)) == 0u) {
            return 0;
        }
        if (streq_local(line, ".")) {
            return 1;
        }
        if (!ed_insert_slot_local(buffer, insert_at, line)) {
            write_err_str("ed: buffer full\n");
            return 0;
        }
        insert_at++;
    }
}

static int ed_delete_range_local(struct ed_buffer *buffer, uint32_t start, uint32_t end) {
    uint32_t remove_count;
    uint32_t i;

    if (buffer == NULL || start == 0u || end == 0u || start > end || end > buffer->count) {
        return 0;
    }
    remove_count = end - start + 1u;
    for (i = start - 1u; i + remove_count < buffer->count; i++) {
        copy_line_local(buffer->lines[i], buffer->lines[i + remove_count], sizeof(buffer->lines[i]));
    }
    buffer->count -= remove_count;
    if (buffer->count == 0u) {
        buffer->current = 0u;
    } else if (start > buffer->count) {
        buffer->current = buffer->count;
    } else {
        buffer->current = start;
    }
    buffer->dirty = 1u;
    return 1;
}

static int ed_parse_address_local(const struct ed_buffer *buffer,
                                  const char **cursor_io,
                                  int32_t *value_out,
                                  int *present_out) {
    const char *cursor;
    int32_t value;
    int present = 1;

    if (buffer == NULL || cursor_io == NULL || value_out == NULL || present_out == NULL) {
        return 0;
    }
    cursor = ed_skip_spaces_local(*cursor_io);
    value = 0;

    if (*cursor >= '0' && *cursor <= '9') {
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (int32_t)(*cursor - '0');
            cursor++;
        }
    } else if (*cursor == '.') {
        value = (int32_t)buffer->current;
        cursor++;
    } else if (*cursor == '$') {
        value = (int32_t)buffer->count;
        cursor++;
    } else {
        present = 0;
    }

    if (present) {
        for (;;) {
            int sign = 0;
            int32_t delta = 0;

            cursor = ed_skip_spaces_local(cursor);
            if (*cursor == '+') {
                sign = 1;
            } else if (*cursor == '-') {
                sign = -1;
            } else {
                break;
            }
            cursor++;
            cursor = ed_skip_spaces_local(cursor);
            while (*cursor >= '0' && *cursor <= '9') {
                delta = delta * 10 + (int32_t)(*cursor - '0');
                cursor++;
            }
            if (delta == 0) {
                delta = 1;
            }
            value += sign > 0 ? delta : -delta;
        }
    }

    *cursor_io = cursor;
    *value_out = value;
    *present_out = present;
    return 1;
}

static int ed_parse_range_local(const struct ed_buffer *buffer,
                                const char **cursor_io,
                                struct ed_address_range *range_out) {
    const char *cursor;
    struct ed_address_range range;

    if (buffer == NULL || cursor_io == NULL || range_out == NULL) {
        return 0;
    }
    cursor = ed_skip_spaces_local(*cursor_io);
    range.has_any = 0;
    range.has_comma = 0;
    range.start_present = 0;
    range.end_present = 0;
    range.start = 0;
    range.end = 0;

    if (*cursor == ',') {
        range.has_any = 1;
        range.has_comma = 1;
        cursor++;
        if (!ed_parse_address_local(buffer, &cursor, &range.end, &range.end_present)) {
            return 0;
        }
        *cursor_io = cursor;
        *range_out = range;
        return 1;
    }

    if (!ed_parse_address_local(buffer, &cursor, &range.start, &range.start_present)) {
        return 0;
    }
    if (!range.start_present) {
        *cursor_io = cursor;
        *range_out = range;
        return 1;
    }

    range.has_any = 1;
    cursor = ed_skip_spaces_local(cursor);
    if (*cursor == ',') {
        range.has_comma = 1;
        cursor++;
        if (!ed_parse_address_local(buffer, &cursor, &range.end, &range.end_present)) {
            return 0;
        }
    }

    *cursor_io = cursor;
    *range_out = range;
    return 1;
}

static int ed_resolve_line_range_local(const struct ed_buffer *buffer,
                                       const struct ed_address_range *range,
                                       uint32_t *start_out,
                                       uint32_t *end_out) {
    int32_t start;
    int32_t end;

    if (buffer == NULL || range == NULL || start_out == NULL || end_out == NULL) {
        return 0;
    }
    if (buffer->count == 0u) {
        return 0;
    }
    if (!range->has_any) {
        if (buffer->current == 0u) {
            return 0;
        }
        start = (int32_t)buffer->current;
        end = (int32_t)buffer->current;
    } else if (range->has_comma) {
        start = range->start_present ? range->start : 1;
        end = range->end_present ? range->end : (int32_t)buffer->count;
    } else {
        start = range->start;
        end = range->start;
    }

    if (start < 1 || end < 1 || start > (int32_t)buffer->count || end > (int32_t)buffer->count || start > end) {
        return 0;
    }
    *start_out = (uint32_t)start;
    *end_out = (uint32_t)end;
    return 1;
}

static int ed_resolve_append_line_local(const struct ed_buffer *buffer,
                                        const struct ed_address_range *range,
                                        uint32_t *line_out) {
    int32_t line;

    if (buffer == NULL || range == NULL || line_out == NULL) {
        return 0;
    }
    if (!range->has_any) {
        line = (int32_t)buffer->current;
    } else if (range->has_comma) {
        line = range->end_present ? range->end : (range->start_present ? range->start : (int32_t)buffer->count);
    } else {
        line = range->start;
    }
    if (line < 0 || line > (int32_t)buffer->count) {
        return 0;
    }
    *line_out = (uint32_t)line;
    return 1;
}

static int ed_resolve_insert_line_local(const struct ed_buffer *buffer,
                                        const struct ed_address_range *range,
                                        uint32_t *line_out) {
    int32_t line;

    if (buffer == NULL || range == NULL || line_out == NULL) {
        return 0;
    }
    if (!range->has_any) {
        line = buffer->current != 0u ? (int32_t)buffer->current : 1;
    } else if (range->has_comma) {
        line = range->start_present ? range->start : 1;
    } else {
        line = range->start;
    }
    if (buffer->count == 0u) {
        if (line != 0 && line != 1) {
            return 0;
        }
        *line_out = 0u;
        return 1;
    }
    if (line < 1 || line > (int32_t)buffer->count + 1) {
        return 0;
    }
    *line_out = line <= 1 ? 0u : (uint32_t)line - 1u;
    return 1;
}

static int ed_change_range_local(struct ed_buffer *buffer, uint32_t start, uint32_t end) {
    uint32_t insert_at;

    if (buffer == NULL || start == 0u || end == 0u || start > end || end > buffer->count) {
        return 0;
    }
    insert_at = start - 1u;
    if (!ed_delete_range_local(buffer, start, end)) {
        return 0;
    }
    return ed_input_mode_local(buffer, insert_at);
}

static void ed_print_help_local(void) {
    write_str("ed form: [addr[,addr]]cmd\n");
    write_str("addr: number, ., $, .+n, .-n, $-n, , for 1,$\n");
    write_str("cmd: p n d c a i w [path] q h\n");
    write_str("append/insert/change mode ends with a single '.' line\n");
}

int cmd_ed(int argc, char **argv) {
    struct ed_buffer *buffer = &g_ed_buffer;
    char line[ED_LINE_MAX];
    int quit_armed = 0;

    buffer->path[0] = '\0';
    buffer->count = 0u;
    buffer->current = 0u;
    buffer->dirty = 0u;

    if (argc > 2) {
        write_err_usage("ed", " [path]\n");
        return 1;
    }
    if (argc == 2 && argv[1][0] != '\0') {
        copy_line_local(buffer->path, argv[1], sizeof(buffer->path));
        if (!ed_load_file_local(buffer, argv[1])) {
            buffer->count = 0u;
            buffer->current = 0u;
            buffer->dirty = 0u;
        }
    }

    for (;;) {
        const char *cursor;
        const char *rest;
        struct ed_address_range range;
        char cmd;

        write_str(": ");
        if (read_line(STDIN_FILENO, line, sizeof(line)) == 0u) {
            write_err_str("?\n");
            return 1;
        }
        trim_line(line);
        if (line[0] == '\0') {
            uint32_t start;
            uint32_t end;

            range.has_any = 0;
            range.has_comma = 0;
            range.start_present = 0;
            range.end_present = 0;
            range.start = 0;
            range.end = 0;
            if (!ed_resolve_line_range_local(buffer, &range, &start, &end)) {
                write_err_str("?\n");
                continue;
            }
            buffer->current = end;
            ed_print_range_local(buffer, start, end, 0);
            quit_armed = 0;
            continue;
        }

        cursor = line;
        if (!ed_parse_range_local(buffer, &cursor, &range)) {
            write_err_str("?\n");
            continue;
        }
        cursor = ed_skip_spaces_local(cursor);
        cmd = *cursor;
        if (cmd == '\0') {
            cmd = 'p';
            rest = cursor;
        } else {
            cursor++;
            rest = ed_skip_spaces_local(cursor);
        }

        if (cmd == 'h') {
            ed_print_help_local();
            quit_armed = 0;
            continue;
        }
        if (cmd == 'p' || cmd == 'n') {
            uint32_t start;
            uint32_t end;

            if (rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (!ed_resolve_line_range_local(buffer, &range, &start, &end)) {
                write_err_str("?\n");
                continue;
            }
            buffer->current = end;
            ed_print_range_local(buffer, start, end, cmd == 'n');
            quit_armed = 0;
            continue;
        }
        if (cmd == 'd') {
            uint32_t start;
            uint32_t end;

            if (rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (!ed_resolve_line_range_local(buffer, &range, &start, &end) ||
                !ed_delete_range_local(buffer, start, end)) {
                write_err_str("?\n");
                continue;
            }
            quit_armed = 0;
            continue;
        }
        if (cmd == 'c') {
            uint32_t start;
            uint32_t end;

            if (rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (!ed_resolve_line_range_local(buffer, &range, &start, &end) ||
                !ed_change_range_local(buffer, start, end)) {
                write_err_str("?\n");
                continue;
            }
            quit_armed = 0;
            continue;
        }
        if (cmd == 'a') {
            uint32_t line_no;

            if (rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (!ed_resolve_append_line_local(buffer, &range, &line_no) ||
                !ed_input_mode_local(buffer, line_no)) {
                write_err_str("?\n");
                continue;
            }
            quit_armed = 0;
            continue;
        }
        if (cmd == 'i') {
            uint32_t insert_at;

            if (rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (!ed_resolve_insert_line_local(buffer, &range, &insert_at) ||
                !ed_input_mode_local(buffer, insert_at)) {
                write_err_str("?\n");
                continue;
            }
            quit_armed = 0;
            continue;
        }
        if (cmd == 'w') {
            const char *path = rest;

            if (range.has_any) {
                write_err_str("?\n");
                continue;
            }
            if (path[0] == '\0') {
                path = buffer->path;
            }
            if (path[0] == '\0') {
                write_err_str("ed: no path\n");
                continue;
            }
            if (!ed_write_file_local(buffer, path)) {
                write_err_str("ed: write failed\n");
                continue;
            }
            quit_armed = 0;
            continue;
        }
        if (cmd == 'q') {
            if (range.has_any || rest[0] != '\0') {
                write_err_str("?\n");
                continue;
            }
            if (buffer->dirty && !quit_armed) {
                write_err_str("ed: buffer modified; use q again to quit\n");
                quit_armed = 1;
                continue;
            }
            return 0;
        }

        write_err_str("?\n");
    }
}
