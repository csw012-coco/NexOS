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

static uint32_t ed_read_stdin_line_local(char *line, uint32_t size) {
    ssize_t got;

    if (line == NULL || size == 0u) {
        return 0u;
    }
    got = nex_read(STDIN_FILENO, line, size, NEX_READ_BLOCKING);
    if (got <= 0) {
        line[0] = '\0';
        return 0u;
    }
    line[size - 1u] = '\0';
    return (uint32_t)got;
}

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
        if (ed_read_stdin_line_local(line, sizeof(line)) == 0u) {
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

enum {
    VI_KEY_ESC = 0x1b,
    VI_KEY_UP = 0x101,
    VI_KEY_DOWN,
    VI_KEY_RIGHT,
    VI_KEY_LEFT,
    VI_MODE_NORMAL = 0,
    VI_MODE_INSERT = 1
};

struct vi_screen {
    uint32_t cols;
    uint32_t rows;
    uint32_t text_rows;
    uint32_t status_row;
};

static void vi_query_screen_local(struct vi_screen *screen) {
    struct syscall_machine_info info;

    if (screen == NULL) {
        return;
    }
    screen->cols = 80u;
    screen->rows = 25u;
    if (machine_info_query(&info) > 0) {
        if (info.text_columns >= 20u) {
            screen->cols = info.text_columns;
        }
        if (info.text_rows >= 5u) {
            screen->rows = info.text_rows;
        }
    }
    screen->text_rows = screen->rows > 1u ? screen->rows - 1u : 1u;
    screen->status_row = screen->rows;
}

static uint32_t vi_line_len_local(const struct ed_buffer *buffer, uint32_t row) {
    if (buffer == NULL || row >= buffer->count) {
        return 0u;
    }
    return str_len_local(buffer->lines[row]);
}

static void vi_set_status_local(char *status, uint32_t size, const char *text) {
    copy_line_local(status, text != NULL ? text : "", size);
}

static uint32_t vi_write_limited_local(const char *text, uint32_t limit) {
    uint32_t written = 0u;

    while (text != NULL && text[written] != '\0' && written < limit) {
        write_stdout(&text[written], 1u);
        written++;
    }
    return written;
}

static int vi_read_key_local(void) {
    char ch = 0;
    char seq1 = 0;
    char seq2 = 0;

    if (nex_read(STDIN_FILENO, &ch, 1u, NEX_READ_BLOCKING | NEX_READ_CHAR) <= 0) {
        return 0;
    }
    if (ch != (char)VI_KEY_ESC) {
        return (uint8_t)ch;
    }
    if (nex_read(STDIN_FILENO, &seq1, 1u, NEX_READ_NONBLOCK | NEX_READ_CHAR) <= 0) {
        return VI_KEY_ESC;
    }
    if (seq1 != '[') {
        return VI_KEY_ESC;
    }
    if (nex_read(STDIN_FILENO, &seq2, 1u, NEX_READ_NONBLOCK | NEX_READ_CHAR) <= 0) {
        return VI_KEY_ESC;
    }
    switch (seq2) {
        case 'A': return VI_KEY_UP;
        case 'B': return VI_KEY_DOWN;
        case 'C': return VI_KEY_RIGHT;
        case 'D': return VI_KEY_LEFT;
        default: return VI_KEY_ESC;
    }
}

static void vi_clamp_cursor_local(struct ed_buffer *buffer, uint32_t *row_io, uint32_t *col_io) {
    uint32_t len;

    if (buffer == NULL || row_io == NULL || col_io == NULL) {
        return;
    }
    if (buffer->count == 0u) {
        buffer->count = 1u;
        buffer->lines[0][0] = '\0';
    }
    if (*row_io >= buffer->count) {
        *row_io = buffer->count - 1u;
    }
    len = vi_line_len_local(buffer, *row_io);
    if (*col_io > len) {
        *col_io = len;
    }
}

static int vi_move_key_local(struct ed_buffer *buffer, int key, uint32_t *row_io, uint32_t *col_io) {
    if (buffer == NULL || row_io == NULL || col_io == NULL) {
        return 0;
    }
    if (key == VI_KEY_LEFT) {
        if (*col_io != 0u) {
            (*col_io)--;
        }
        return 1;
    }
    if (key == VI_KEY_RIGHT) {
        if (*col_io < vi_line_len_local(buffer, *row_io)) {
            (*col_io)++;
        }
        return 1;
    }
    if (key == VI_KEY_UP) {
        if (*row_io != 0u) {
            (*row_io)--;
            vi_clamp_cursor_local(buffer, row_io, col_io);
        }
        return 1;
    }
    if (key == VI_KEY_DOWN) {
        if (*row_io + 1u < buffer->count) {
            (*row_io)++;
            vi_clamp_cursor_local(buffer, row_io, col_io);
        }
        return 1;
    }
    return 0;
}

static int vi_update_view_local(uint32_t row,
                                uint32_t col,
                                const struct vi_screen *screen,
                                uint32_t *top_row_io,
                                uint32_t *left_col_io) {
    int changed = 0;

    if (screen == NULL || top_row_io == NULL || left_col_io == NULL) {
        return 0;
    }
    if (row < *top_row_io) {
        *top_row_io = row;
        changed = 1;
    } else if (row >= *top_row_io + screen->text_rows) {
        *top_row_io = row - screen->text_rows + 1u;
        changed = 1;
    }
    if (col < *left_col_io) {
        *left_col_io = col;
        changed = 1;
    } else if (col >= *left_col_io + screen->cols) {
        *left_col_io = col - screen->cols + 1u;
        changed = 1;
    }
    return changed;
}

static uint32_t vi_visible_col_local(uint32_t cursor_col, uint32_t left_col, uint32_t cols) {
    uint32_t out = cursor_col >= left_col ? cursor_col - left_col : 0u;

    if (cols == 0u) {
        return 0u;
    }
    return out < cols ? out : cols - 1u;
}

static void vi_render_text_line_local(struct ed_buffer *buffer,
                                      uint32_t screen_row,
                                      uint32_t buffer_row,
                                      uint32_t left_col,
                                      const struct vi_screen *screen) {
    uint32_t cols = screen != NULL ? screen->cols : 80u;
    uint32_t written = 0u;

    printf("\x1b[%u;1H", screen_row);
    if (buffer_row < buffer->count) {
        const char *line = buffer->lines[buffer_row];
        uint32_t len = str_len_local(line);

        if (left_col < len) {
            while (written < cols && line[left_col + written] != '\0') {
                write_stdout(&line[left_col + written], 1u);
                written++;
            }
        }
    } else {
        write_str("~");
        written = 1u;
    }
    if (written < cols) {
        write_str("\x1b[K");
    }
}

static void vi_render_status_local(struct ed_buffer *buffer,
                                   const struct vi_screen *screen,
                                   uint32_t mode,
                                   const char *status) {
    uint32_t cols = screen != NULL ? screen->cols : 80u;
    uint32_t status_row = screen != NULL ? screen->status_row : 25u;
    uint32_t remaining = cols;
    uint32_t written;

    printf("\x1b[%u;1H", status_row);
    written = vi_write_limited_local(mode == VI_MODE_INSERT ? "-- INSERT -- " : "-- NORMAL -- ", remaining);
    remaining = remaining > written ? remaining - written : 0u;
    written = vi_write_limited_local(buffer->path[0] != '\0' ? buffer->path : "[No Name]", remaining);
    remaining = remaining > written ? remaining - written : 0u;
    written = vi_write_limited_local(buffer->dirty ? " [+] " : " ", remaining);
    remaining = remaining > written ? remaining - written : 0u;
    if (status != NULL) {
        (void)vi_write_limited_local(status, remaining);
    }
    write_str("\x1b[K");
}

static void vi_move_cursor_local(uint32_t cursor_row,
                                 uint32_t cursor_col,
                                 uint32_t top_row,
                                 uint32_t left_col,
                                 const struct vi_screen *screen) {
    uint32_t cols = screen != NULL ? screen->cols : 80u;

    printf("\x1b[%u;%uH",
           (cursor_row - top_row) + 1u,
           vi_visible_col_local(cursor_col, left_col, cols) + 1u);
}

static void vi_render_local(struct ed_buffer *buffer,
                            uint32_t cursor_row,
                            uint32_t cursor_col,
                            uint32_t top_row,
                            uint32_t left_col,
                            const struct vi_screen *screen,
                            uint32_t mode,
                            const char *status) {
    uint32_t i;
    uint32_t text_rows = screen != NULL ? screen->text_rows : 24u;

    write_str("\x1b[2J\x1b[H");
    for (i = 0u; i < text_rows; i++) {
        vi_render_text_line_local(buffer, i + 1u, top_row + i, left_col, screen);
    }
    vi_render_status_local(buffer, screen, mode, status);
    vi_move_cursor_local(cursor_row, cursor_col, top_row, left_col, screen);
}

static int vi_insert_char_local(struct ed_buffer *buffer, uint32_t row, uint32_t *col_io, char ch) {
    char *line;
    uint32_t len;
    uint32_t col;

    if (buffer == NULL || col_io == NULL || row >= buffer->count) {
        return 0;
    }
    line = buffer->lines[row];
    len = str_len_local(line);
    col = *col_io;
    if (len + 1u >= ED_LINE_MAX || col > len) {
        return 0;
    }
    for (uint32_t i = len + 1u; i > col; i--) {
        line[i] = line[i - 1u];
    }
    line[col] = ch;
    *col_io = col + 1u;
    buffer->dirty = 1u;
    return 1;
}

static int vi_delete_char_local(struct ed_buffer *buffer, uint32_t row, uint32_t col) {
    char *line;
    uint32_t len;

    if (buffer == NULL || row >= buffer->count) {
        return 0;
    }
    line = buffer->lines[row];
    len = str_len_local(line);
    if (col >= len) {
        return 0;
    }
    for (uint32_t i = col; i < len; i++) {
        line[i] = line[i + 1u];
    }
    buffer->dirty = 1u;
    return 1;
}

static int vi_split_line_local(struct ed_buffer *buffer, uint32_t *row_io, uint32_t *col_io) {
    char tail[ED_LINE_MAX];
    uint32_t row;
    uint32_t col;

    if (buffer == NULL || row_io == NULL || col_io == NULL || buffer->count >= ED_BUFFER_LINES) {
        return 0;
    }
    row = *row_io;
    col = *col_io;
    if (row >= buffer->count || col > str_len_local(buffer->lines[row])) {
        return 0;
    }
    copy_line_local(tail, buffer->lines[row] + col, sizeof(tail));
    buffer->lines[row][col] = '\0';
    if (!ed_insert_slot_local(buffer, row + 1u, tail)) {
        return 0;
    }
    *row_io = row + 1u;
    *col_io = 0u;
    return 1;
}

static int vi_join_backspace_local(struct ed_buffer *buffer, uint32_t *row_io, uint32_t *col_io) {
    uint32_t row;
    uint32_t prev_len;
    uint32_t cur_len;

    if (buffer == NULL || row_io == NULL || col_io == NULL || *row_io == 0u) {
        return 0;
    }
    row = *row_io;
    prev_len = str_len_local(buffer->lines[row - 1u]);
    cur_len = str_len_local(buffer->lines[row]);
    if (prev_len + cur_len >= ED_LINE_MAX) {
        return 0;
    }
    copy_line_local(buffer->lines[row - 1u] + prev_len,
                    buffer->lines[row],
                    sizeof(buffer->lines[row - 1u]) - prev_len);
    (void)ed_delete_range_local(buffer, row + 1u, row + 1u);
    *row_io = row - 1u;
    *col_io = prev_len;
    return 1;
}

static int vi_delete_line_local(struct ed_buffer *buffer, uint32_t *row_io, uint32_t *col_io) {
    if (buffer == NULL || row_io == NULL || col_io == NULL || buffer->count == 0u) {
        return 0;
    }
    if (!ed_delete_range_local(buffer, *row_io + 1u, *row_io + 1u)) {
        return 0;
    }
    if (buffer->count == 0u) {
        buffer->count = 1u;
        buffer->lines[0][0] = '\0';
    }
    if (*row_io >= buffer->count) {
        *row_io = buffer->count - 1u;
    }
    *col_io = 0u;
    buffer->dirty = 1u;
    return 1;
}

static uint32_t vi_write_file_local(struct ed_buffer *buffer, const char *path) {
    uint32_t i;
    uint32_t written = 0u;
    int fd;

    if (buffer == NULL || path == NULL || path[0] == '\0') {
        return 0u;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        return 0u;
    }
    for (i = 0u; i < buffer->count; i++) {
        uint32_t len = str_len_local(buffer->lines[i]);

        if (len != 0u && write_fd((uint32_t)fd, buffer->lines[i], len) != len) {
            close((uint32_t)fd);
            return 0u;
        }
        written += len;
        if (write_fd((uint32_t)fd, "\n", 1u) != 1u) {
            close((uint32_t)fd);
            return 0u;
        }
        written++;
    }
    close((uint32_t)fd);
    copy_line_local(buffer->path, path, sizeof(buffer->path));
    buffer->dirty = 0u;
    return written;
}

static int vi_read_command_local(char *out, uint32_t size, const struct vi_screen *screen) {
    uint32_t len = 0u;
    uint32_t row = screen != NULL ? screen->status_row : 25u;

    if (out == NULL || size == 0u) {
        return 0;
    }
    printf("\x1b[%u;1H\x1b[K:", row);
    for (;;) {
        int key = vi_read_key_local();

        if (key == '\n' || key == '\r') {
            out[len] = '\0';
            return 1;
        }
        if (key == VI_KEY_ESC) {
            out[0] = '\0';
            return 0;
        }
        if (key == '\b' || key == 0x7f) {
            if (len != 0u) {
                len--;
                write_str("\b \b");
            }
            continue;
        }
        if (key >= 0x20 && key <= 0x7e && len + 1u < size) {
            out[len++] = (char)key;
            write_stdout(&out[len - 1u], 1u);
        }
    }
}

int cmd_vi(int argc, char **argv) {
    struct ed_buffer *buffer;
    uint32_t row = 0u;
    uint32_t col = 0u;
    uint32_t top_row = 0u;
    uint32_t left_col = 0u;
    uint32_t mode = VI_MODE_NORMAL;
    uint8_t pending_d = 0u;
    char status[80];
    struct vi_screen screen;
    uint32_t dirty_row = 0u;
    uint32_t last_cols = 0u;
    uint32_t last_rows = 0u;
    int full_repaint = 1;
    int dirty_line = 0;
    int dirty_status = 1;
    int dirty_cursor = 1;

    if (argc > 2) {
        write_err_usage("vi", " [path]\n");
        return 1;
    }
    buffer = (struct ed_buffer *)calloc(1u, sizeof(*buffer));
    if (buffer == NULL) {
        write_err_str("vi: out of memory\n");
        return 1;
    }
    status[0] = '\0';
    if (argc == 2 && argv[1][0] != '\0') {
        copy_line_local(buffer->path, argv[1], sizeof(buffer->path));
        if (!ed_load_file_local(buffer, argv[1])) {
            buffer->count = 0u;
            buffer->dirty = 0u;
        }
    }
    if (buffer->count == 0u) {
        buffer->count = 1u;
        buffer->lines[0][0] = '\0';
    }
    clear();

    for (;;) {
        int key;

        vi_query_screen_local(&screen);
        if (screen.cols != last_cols || screen.rows != last_rows) {
            last_cols = screen.cols;
            last_rows = screen.rows;
            full_repaint = 1;
        }
        vi_clamp_cursor_local(buffer, &row, &col);
        if (vi_update_view_local(row, col, &screen, &top_row, &left_col)) {
            full_repaint = 1;
        }
        if (full_repaint) {
            vi_render_local(buffer, row, col, top_row, left_col, &screen, mode, status);
        } else {
            if (dirty_line && dirty_row >= top_row && dirty_row < top_row + screen.text_rows) {
                vi_render_text_line_local(buffer, (dirty_row - top_row) + 1u, dirty_row, left_col, &screen);
                dirty_cursor = 1;
            }
            if (dirty_status) {
                vi_render_status_local(buffer, &screen, mode, status);
                dirty_cursor = 1;
            }
            if (dirty_cursor) {
                vi_move_cursor_local(row, col, top_row, left_col, &screen);
            }
        }
        full_repaint = 0;
        dirty_line = 0;
        dirty_status = 0;
        dirty_cursor = 0;
        key = vi_read_key_local();
        status[0] = '\0';

        if (mode == VI_MODE_INSERT) {
            if (key == VI_KEY_ESC) {
                mode = VI_MODE_NORMAL;
                dirty_status = 1;
                dirty_cursor = 1;
                continue;
            }
            if (vi_move_key_local(buffer, key, &row, &col)) {
                dirty_cursor = 1;
                continue;
            }
            if (key == '\n' || key == '\r') {
                if (!vi_split_line_local(buffer, &row, &col)) {
                    vi_set_status_local(status, sizeof(status), "line limit");
                    dirty_status = 1;
                    dirty_cursor = 1;
                } else {
                    full_repaint = 1;
                }
                continue;
            }
            if (key == '\b' || key == 0x7f) {
                if (col != 0u) {
                    col--;
                    if (vi_delete_char_local(buffer, row, col)) {
                        dirty_line = 1;
                        dirty_row = row;
                        dirty_status = 1;
                    }
                    dirty_cursor = 1;
                } else {
                    if (vi_join_backspace_local(buffer, &row, &col)) {
                        full_repaint = 1;
                    } else {
                        dirty_cursor = 1;
                    }
                }
                continue;
            }
            if (key >= 0x20 && key <= 0x7e) {
                if (!vi_insert_char_local(buffer, row, &col, (char)key)) {
                    vi_set_status_local(status, sizeof(status), "line full");
                    dirty_status = 1;
                } else {
                    dirty_line = 1;
                    dirty_row = row;
                    dirty_status = 1;
                }
                dirty_cursor = 1;
            }
            continue;
        }

        if (pending_d && key != 'd') {
            pending_d = 0u;
            dirty_status = 1;
        }
        if (key == 'i') {
            mode = VI_MODE_INSERT;
            dirty_status = 1;
            dirty_cursor = 1;
            continue;
        }
        if (key == 'a') {
            if (col < vi_line_len_local(buffer, row)) {
                col++;
            }
            mode = VI_MODE_INSERT;
            dirty_status = 1;
            dirty_cursor = 1;
            continue;
        }
        if (key == 'o' || key == 'O') {
            uint32_t insert_at = key == 'o' ? row + 1u : row;

            if (ed_insert_slot_local(buffer, insert_at, "")) {
                row = insert_at;
                col = 0u;
                mode = VI_MODE_INSERT;
                full_repaint = 1;
            } else {
                vi_set_status_local(status, sizeof(status), "buffer full");
                dirty_status = 1;
                dirty_cursor = 1;
            }
            continue;
        }
        if (key == 'h' || key == VI_KEY_LEFT) {
            (void)vi_move_key_local(buffer, VI_KEY_LEFT, &row, &col);
            dirty_cursor = 1;
            continue;
        }
        if (key == 'l' || key == VI_KEY_RIGHT) {
            (void)vi_move_key_local(buffer, VI_KEY_RIGHT, &row, &col);
            dirty_cursor = 1;
            continue;
        }
        if (key == 'k' || key == VI_KEY_UP) {
            (void)vi_move_key_local(buffer, VI_KEY_UP, &row, &col);
            dirty_cursor = 1;
            continue;
        }
        if (key == 'j' || key == VI_KEY_DOWN) {
            (void)vi_move_key_local(buffer, VI_KEY_DOWN, &row, &col);
            dirty_cursor = 1;
            continue;
        }
        if (key == '0') {
            col = 0u;
            dirty_cursor = 1;
            continue;
        }
        if (key == '$') {
            col = vi_line_len_local(buffer, row);
            dirty_cursor = 1;
            continue;
        }
        if (key == 'x') {
            if (!vi_delete_char_local(buffer, row, col)) {
                vi_set_status_local(status, sizeof(status), "nothing to delete");
                dirty_status = 1;
            } else {
                dirty_line = 1;
                dirty_row = row;
                dirty_status = 1;
            }
            dirty_cursor = 1;
            continue;
        }
        if (key == 'd') {
            if (pending_d) {
                (void)vi_delete_line_local(buffer, &row, &col);
                pending_d = 0u;
                full_repaint = 1;
            } else {
                pending_d = 1u;
                vi_set_status_local(status, sizeof(status), "d");
                dirty_status = 1;
                dirty_cursor = 1;
            }
            continue;
        }
        if (key == ':') {
            char command[64];
            const char *path;
            uint32_t written;

            if (!vi_read_command_local(command, sizeof(command), &screen)) {
                full_repaint = 1;
                continue;
            }
            full_repaint = 1;
            trim_line(command);
            if (streq_local(command, "q")) {
                if (buffer->dirty) {
                    vi_set_status_local(status, sizeof(status), "modified; use :q!");
                    dirty_status = 1;
                    continue;
                }
                write_str("\x1b[2J\x1b[H");
                free(buffer);
                return 0;
            }
            if (streq_local(command, "q!")) {
                write_str("\x1b[2J\x1b[H");
                free(buffer);
                return 0;
            }
            if (streq_local(command, "w") ||
                streq_local(command, "wq") ||
                streq_local(command, "x") ||
                (command[0] == 'w' && command[1] == ' ')) {
                path = command[0] == 'w' && command[1] == ' ' ? ed_skip_spaces_local(command + 2) : buffer->path;
                if (path[0] == '\0') {
                    vi_set_status_local(status, sizeof(status), "no file name");
                    dirty_status = 1;
                    continue;
                }
                written = vi_write_file_local(buffer, path);
                if (written == 0u && buffer->count != 0u) {
                    vi_set_status_local(status, sizeof(status), "write failed");
                    dirty_status = 1;
                    continue;
                }
                snprintf(status, sizeof(status), "%u bytes written", written);
                dirty_status = 1;
                if (streq_local(command, "wq") || streq_local(command, "x")) {
                    write_str("\x1b[2J\x1b[H");
                    free(buffer);
                    return 0;
                }
                continue;
            }
            vi_set_status_local(status, sizeof(status), "commands: :w :q :q! :wq");
            dirty_status = 1;
        }
    }
}

int cmd_ed(int argc, char **argv) {
    struct ed_buffer *buffer;
    char line[ED_LINE_MAX];
    int quit_armed = 0;

    if (argc > 2) {
        write_err_usage("ed", " [path]\n");
        return 1;
    }
    buffer = (struct ed_buffer *)calloc(1u, sizeof(*buffer));
    if (buffer == NULL) {
        write_err_str("ed: out of memory\n");
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
        if (ed_read_stdin_line_local(line, sizeof(line)) == 0u) {
            write_err_str("?\n");
            free(buffer);
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
            free(buffer);
            return 0;
        }

        write_err_str("?\n");
    }
}
