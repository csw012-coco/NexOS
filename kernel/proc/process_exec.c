#include "kernel/internal/proc/process_elf_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/internal/proc/process_program_registry_internal.h"
#include "fs/vfs.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/job_control.h"
#include "lib/string.h"

static const uint8_t g_ring3_smoke_code[] = {
    0xb8, SYS_EXIT, 0x00, 0x00, 0x00,
    0x31, 0xdb,
    0xcd, 0x40,
    0x0f, 0x0b
};

enum {
    PROCESS_EXEC_PROBE_SIZE = NOS_TTY_LINE_BUFFER_SIZE,
    PROCESS_EXEC_SCRIPT_DEPTH_MAX = 4
};

static int process_exec_elf(struct vfs *vfs,
                            const char *image_name,
                            const char *command_line,
                            const char *const *envp);

static int process_exec_args_valid(struct vfs *vfs, const char *image_name) {
    if (vfs == NULL || image_name == NULL) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    return 1;
}

static uint32_t process_exec_node_file_size(const struct vfs_node *node) {
    return vfs_node_file_size(node);
}

static int process_open_exec_file(struct vfs *vfs,
                                  const char *image_name,
                                  struct vfs_node *node_out,
                                  uint32_t *file_size_out) {
    struct vfs_node node;

    if (!process_exec_args_valid(vfs, image_name)) {
        return 0;
    }
    if (vfs_open(vfs, image_name, 0, &node) != 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_NOT_FOUND;
        return 0;
    }
    if (node.kind != VFS_NODE_FILE) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_NOT_FOUND;
        return 0;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    if (file_size_out != 0) {
        *file_size_out = process_exec_node_file_size(&node);
    }
    return 1;
}

static int process_read_exec_file(struct vfs *vfs,
                                  const char *image_name,
                                  struct vfs_node *node_out,
                                  uint32_t *bytes_read_out) {
    struct vfs_node node;
    uint32_t offset = 0;
    uint32_t file_size = 0;
    int64_t read_rc;

    g_process_exec_read_open_rc = 0xffffffffu;
    g_process_exec_read_node_kind = 0;
    g_process_exec_read_mount_kind = 0;
    g_process_exec_read_file_size = 0;
    g_process_exec_read_bytes = 0;
    g_process_exec_read_result = 0xffffffffu;
    if (!process_open_exec_file(vfs, image_name, &node, &file_size)) {
        g_process_exec_read_open_rc = 0xffffffffu;
        return 0;
    }
    g_process_exec_read_open_rc = 0u;
    g_process_exec_read_node_kind = node.kind;
    g_process_exec_read_mount_kind = node.mount_kind;
    g_process_exec_read_file_size = file_size;
    if (file_size > sizeof(g_elf_file_buffer)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_TOO_LARGE;
        g_process_exec_read_result = PROCESS_EXEC_ERR_FILE_TOO_LARGE;
        return 0;
    }
    read_rc = vfs_read(vfs, &node, &offset, g_elf_file_buffer, file_size, VFS_READ_BLOCKING);
    if (read_rc < 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_READ;
        g_process_exec_read_result = PROCESS_EXEC_ERR_FILE_READ;
        return 0;
    }
    g_process_exec_read_bytes = (uint32_t)read_rc;
    if ((uint32_t)read_rc != file_size) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_READ;
        g_process_exec_read_result = PROCESS_EXEC_ERR_FILE_READ;
        return 0;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    if (bytes_read_out != 0) {
        *bytes_read_out = file_size;
    }
    g_process_exec_read_result = PROCESS_EXEC_OK;
    return 1;
}

static int process_map_exec_stack(void) {
    if (g_bound_session->address_space.user_cr3 == 0 ||
        !vmm_root_is_current(g_bound_session->address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    if (!addrspace_map_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }
    return 1;
}

static uint32_t process_text_len_local(const char *text) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static void process_copy_text_local(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int process_append_char_local(char *dst, uint32_t dst_size, uint32_t *len_io, char ch) {
    uint32_t len;

    if (dst == 0 || len_io == 0 || *len_io >= dst_size) {
        return 0;
    }
    len = *len_io;
    if (len + 1u >= dst_size) {
        return 0;
    }
    dst[len++] = ch;
    dst[len] = '\0';
    *len_io = len;
    return 1;
}

static int process_append_text_local(char *dst, uint32_t dst_size, uint32_t *len_io, const char *src) {
    uint32_t len;
    uint32_t i = 0;

    if (dst == 0 || len_io == 0 || src == 0 || *len_io >= dst_size) {
        return 0;
    }
    len = *len_io;
    while (src[i] != '\0') {
        if (len + 1u >= dst_size) {
            return 0;
        }
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
    *len_io = len;
    return 1;
}

static const char *process_skip_spaces_local(const char *text) {
    while (text != 0 &&
           (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) {
        text++;
    }
    return text;
}

static int process_read_token_local(const char **text_io, char *token_out, uint32_t token_size) {
    const char *cursor;
    uint32_t out_len = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (text_io == 0 || token_out == 0 || token_size == 0) {
        return 0;
    }
    cursor = process_skip_spaces_local(*text_io);
    if (cursor == 0 || *cursor == '\0') {
        token_out[0] = '\0';
        *text_io = cursor;
        return 0;
    }

    while (*cursor != '\0') {
        char ch = *cursor;

        if (!single_quote && ch == '\\') {
            cursor++;
            if (*cursor == '\0') {
                break;
            }
            ch = *cursor++;
        } else if (!double_quote && ch == '\'') {
            single_quote = !single_quote;
            cursor++;
            continue;
        } else if (!single_quote && ch == '"') {
            double_quote = !double_quote;
            cursor++;
            continue;
        } else if (!single_quote && !double_quote &&
                   (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
            break;
        } else {
            cursor++;
        }

        if (out_len + 1u >= token_size) {
            return 0;
        }
        token_out[out_len++] = ch;
    }

    if (single_quote || double_quote) {
        return 0;
    }

    token_out[out_len] = '\0';
    *text_io = cursor;
    return out_len != 0;
}

static int process_probe_exec_file(struct vfs *vfs,
                                   const char *image_name,
                                   struct vfs_node *node_out,
                                   uint32_t *file_size_out,
                                   uint8_t *probe_out,
                                   uint32_t probe_size,
                                   uint32_t *probe_bytes_out) {
    struct vfs_node node;
    uint32_t offset = 0;
    uint32_t file_size = 0;
    uint32_t to_read;
    int64_t read_rc;

    if (probe_bytes_out != 0) {
        *probe_bytes_out = 0;
    }
    if (!process_open_exec_file(vfs, image_name, &node, &file_size)) {
        return 0;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    if (file_size_out != 0) {
        *file_size_out = file_size;
    }
    if (probe_out == 0 || probe_size == 0) {
        return 1;
    }

    to_read = file_size < probe_size ? file_size : probe_size;
    if (to_read == 0u) {
        return 1;
    }
    read_rc = vfs_read(vfs, &node, &offset, probe_out, to_read, VFS_READ_BLOCKING);
    if (read_rc < 0 || (uint32_t)read_rc != to_read) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_READ;
        return 0;
    }
    if (probe_bytes_out != 0) {
        *probe_bytes_out = to_read;
    }
    return 1;
}

static int process_probe_has_shebang(const uint8_t *probe, uint32_t probe_bytes) {
    return probe != 0 && probe_bytes >= 2u && probe[0] == '#' && probe[1] == '!';
}

static int process_build_shebang_command(const char *script_path,
                                         const char *command_line,
                                         const uint8_t *probe,
                                         uint32_t probe_bytes,
                                         char *out,
                                         uint32_t out_size) {
    char header[PROCESS_EXEC_PROBE_SIZE + 1u];
    char interpreter[NOS_PATH_BUFFER_SIZE];
    char original_name[NOS_PATH_BUFFER_SIZE];
    const char *cursor;
    const char *rest_args;
    uint32_t header_len = 0;
    uint32_t out_len = 0;

    if (script_path == 0 || probe == 0 || out == 0 || out_size == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_HEADER;
        return 0;
    }
    while (header_len < probe_bytes &&
           probe[header_len] != '\0' &&
           probe[header_len] != '\n' &&
           probe[header_len] != '\r') {
        header[header_len] = (char)probe[header_len];
        header_len++;
    }
    header[header_len] = '\0';
    if (header_len < 3u || header[0] != '#' || header[1] != '!') {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_HEADER;
        return 0;
    }

    cursor = process_skip_spaces_local(header + 2);
    if (!process_read_token_local(&cursor, interpreter, sizeof(interpreter))) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_HEADER;
        return 0;
    }
    out[0] = '\0';
    if (!process_append_text_local(out, out_size, &out_len, interpreter)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    cursor = process_skip_spaces_local(cursor);
    if (cursor != 0 && *cursor != '\0') {
        if (!process_append_char_local(out, out_size, &out_len, ' ') ||
            !process_append_text_local(out, out_size, &out_len, cursor)) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
    }
    if (!process_append_char_local(out, out_size, &out_len, ' ') ||
        !process_append_text_local(out, out_size, &out_len, script_path)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    cursor = command_line != 0 ? command_line : script_path;
    if (!process_read_token_local(&cursor, original_name, sizeof(original_name))) {
        return 1;
    }
    rest_args = process_skip_spaces_local(cursor);
    if (rest_args != 0 && *rest_args != '\0') {
        if (!process_append_char_local(out, out_size, &out_len, ' ') ||
            !process_append_text_local(out, out_size, &out_len, rest_args)) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
    }
    return 1;
}

static int process_resolve_exec_target_depth(struct vfs *vfs,
                                             const char *image_name,
                                             const char *command_line,
                                             char *resolved_image_name_out,
                                             uint32_t resolved_image_name_size,
                                             char *resolved_command_line_out,
                                             uint32_t resolved_command_line_size,
                                             struct vfs_node *node_out,
                                             uint32_t *bytes_read_out,
                                             uint32_t depth) {
    uint8_t probe[PROCESS_EXEC_PROBE_SIZE];
    uint32_t probe_bytes = 0;
    char redirected_command[NOS_TTY_LINE_BUFFER_SIZE];
    char redirected_name[NOS_TTY_LINE_BUFFER_SIZE];

    if (depth >= PROCESS_EXEC_SCRIPT_DEPTH_MAX) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_HEADER;
        return 0;
    }
    if (!process_probe_exec_file(vfs,
                                 image_name,
                                 0,
                                 0,
                                 probe,
                                 sizeof(probe),
                                 &probe_bytes)) {
        return 0;
    }
    if (process_probe_has_shebang(probe, probe_bytes)) {
        if (!process_build_shebang_command(image_name,
                                           command_line != 0 ? command_line : image_name,
                                           probe,
                                           probe_bytes,
                                           redirected_command,
                                           sizeof(redirected_command))) {
            return 0;
        }
        if (!process_extract_command_name(redirected_command,
                                          redirected_name,
                                          sizeof(redirected_name))) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
        return process_resolve_exec_target_depth(vfs,
                                                 process_resolve_image_name(redirected_name),
                                                 redirected_command,
                                                 resolved_image_name_out,
                                                 resolved_image_name_size,
                                                 resolved_command_line_out,
                                                 resolved_command_line_size,
                                                 node_out,
                                                 bytes_read_out,
                                                 depth + 1u);
    }
    if (!process_read_exec_file(vfs, image_name, node_out, bytes_read_out)) {
        return 0;
    }
    process_copy_text_local(resolved_image_name_out, resolved_image_name_size, image_name);
    process_copy_text_local(resolved_command_line_out,
                            resolved_command_line_size,
                            command_line != 0 ? command_line : image_name);
    return 1;
}

int process_resolve_exec_target(struct vfs *vfs,
                                const char *image_name,
                                const char *command_line,
                                char *resolved_image_name_out,
                                uint32_t resolved_image_name_size,
                                char *resolved_command_line_out,
                                uint32_t resolved_command_line_size,
                                struct vfs_node *node_out,
                                uint32_t *bytes_read_out) {
    return process_resolve_exec_target_depth(vfs,
                                             image_name,
                                             command_line,
                                             resolved_image_name_out,
                                             resolved_image_name_size,
                                             resolved_command_line_out,
                                             resolved_command_line_size,
                                             node_out,
                                             bytes_read_out,
                                             0u);
}

static uint32_t process_find_last_slash_local(const char *text) {
    uint32_t i = 0;
    uint32_t last = 0;

    while (text != 0 && text[i] != '\0') {
        if (text[i] == '/') {
            last = i;
        }
        i++;
    }
    return last;
}

static void process_path_pop_segment_local(char *path) {
    uint32_t len;
    uint32_t last;

    if (path == 0) {
        return;
    }
    len = process_text_len_local(path);
    if (len <= 1u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    last = process_find_last_slash_local(path);
    if (last == 0u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    path[last] = '\0';
}

static int process_path_append_segment_local(char *path, uint32_t path_size, const char *segment, uint32_t seg_len) {
    uint32_t len;
    uint32_t i;

    if (path == 0 || segment == 0 || seg_len == 0) {
        return 0;
    }
    len = process_text_len_local(path);
    if (len == 0 || len >= path_size) {
        return 0;
    }
    if (!(len == 1u && path[0] == '/')) {
        if (len + 1u >= path_size) {
            return 0;
        }
        path[len++] = '/';
    }
    if (len + seg_len >= path_size) {
        return 0;
    }
    for (i = 0; i < seg_len; i++) {
        path[len + i] = segment[i];
    }
    path[len + seg_len] = '\0';
    return 1;
}

static int process_command_name_needs_path(const char *name) {
    uint32_t i = 0;

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

static int process_resolve_process_path_local(const struct process *proc,
                                              const char *input,
                                              char *out,
                                              uint32_t out_size) {
    uint32_t pos = 0;

    if (proc == 0 || input == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        pos = 1u;
    } else {
        process_copy_text_local(out, out_size, process_cwd(proc));
    }
    if (input[0] == '\0' || streq(input, ".")) {
        return 1;
    }
    while (input[pos] != '\0') {
        char segment[NOS_NAME_BUFFER_SIZE];
        uint32_t seg_len = 0;

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
            process_path_pop_segment_local(out);
            continue;
        }
        if (!process_path_append_segment_local(out, out_size, segment, seg_len)) {
            return 0;
        }
    }
    return 1;
}

static int process_resolve_exec_command_line(struct process *proc, char *line, uint32_t line_size) {
    char token[NOS_PATH_MAX + 1];
    char resolved[NOS_PATH_MAX + 1];
    char original[NOS_TTY_LINE_BUFFER_SIZE];
    uint32_t token_len = 0;
    uint32_t rest_pos = 0;
    uint32_t resolved_len;
    uint32_t rest_len;
    uint32_t i;

    if (proc == 0 || line == 0 || line_size == 0) {
        return 0;
    }
    while (line[rest_pos] == ' ' || line[rest_pos] == '\t') {
        rest_pos++;
    }
    process_copy_text_local(original, sizeof(original), line);
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
    if (!process_resolve_process_path_local(proc, token, resolved, sizeof(resolved))) {
        return 0;
    }
    resolved_len = process_text_len_local(resolved);
    rest_len = process_text_len_local(line + rest_pos + token_len);
    if (rest_pos + resolved_len + rest_len + 1u > line_size) {
        return 0;
    }
    for (i = 0; i < resolved_len; i++) {
        line[rest_pos + i] = resolved[i];
    }
    for (i = 0; i <= rest_len; i++) {
        line[rest_pos + resolved_len + i] = original[rest_pos + token_len + i];
    }
    return 1;
}

static int process_map_spawn_mode_local(uint32_t syscall_mode, enum process_exec_mode *mode_out) {
    if (mode_out == 0) {
        return 0;
    }
    switch (syscall_mode) {
        case SYS_SPAWN_AUTO:
            *mode_out = PROCESS_EXEC_AUTO;
            return 1;
        case SYS_SPAWN_ELF:
            *mode_out = PROCESS_EXEC_ELF;
            return 1;
        default:
            return 0;
    }
}

static int process_run_foreground_command(struct vfs *vfs,
                                          struct process *proc,
                                          char *command_line,
                                          const char *const *envp,
                                          enum process_exec_mode mode) {
    if (proc != 0) {
        uint32_t pid = 0;
        enum process_state parent_state;
        int foreground_ok;
        struct process_snapshot exited;

        if (!job_run_background_with_pid(vfs, command_line, envp, mode, &pid)) {
            return 0;
        }

        parent_state = proc->state;
        proc->state = PROCESS_STATE_WAITING;
        foreground_ok = job_foreground_pid(pid);
        if (proc->state == PROCESS_STATE_WAITING) {
            proc->state = parent_state;
        }

        if (!foreground_ok &&
            (!process_get_last_exit(&exited) || exited.pid != pid)) {
            return 0;
        }
        (void)process_wait_pid(pid, &exited);
        return 1;
    }
    return process_exec(vfs, command_line, envp, mode);
}

static int process_dispatch_exec_request(const struct process_exec_request *req) {
    const struct process_program *program;
    char command_name[NOS_TTY_LINE_BUFFER_SIZE];
    const char *image_name;

    if (req == NULL || req->name == NULL) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    if (!process_extract_command_name(req->name, command_name, sizeof(command_name))) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    program = process_find_program_internal(command_name);
    image_name = program != NULL ? program->image_name : command_name;

    switch (req->mode) {
        case PROCESS_EXEC_DIRECT:
        case PROCESS_EXEC_ELF:
        case PROCESS_EXEC_AUTO:
            return process_exec_elf(req->vfs, image_name, req->name, req->envp);
        default:
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
    }
}

int process_run(const char *name) {
    return process_exec(NULL, name, NULL, PROCESS_EXEC_AUTO);
}

int process_exec_elf(struct vfs *vfs,
                     const char *image_name,
                     const char *command_line,
                     const char *const *envp) {
    char resolved_image_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_command_line[NOS_TTY_LINE_BUFFER_SIZE];
    struct vfs_node node;
    uint32_t bytes_read = 0;
    uint64_t entry = 0;
    uint64_t stack_top = 0;

    g_process_exec_last_error = PROCESS_EXEC_OK;
    g_process_exec_last_stage = 1;
    g_process_exec_read_open_rc = 0;
    g_process_exec_read_node_kind = 0;
    g_process_exec_read_mount_kind = 0;
    g_process_exec_read_file_size = 0;
    g_process_exec_read_bytes = 0;
    g_process_exec_read_result = 0;
    process_bind_session(&g_user_session, g_user_page_mappings);
    if (!process_exec_args_valid(vfs, image_name)) {
        return 0;
    }
    g_process_exec_last_stage = 2;
    if (!process_resolve_exec_target(vfs,
                                     image_name,
                                     command_line != NULL ? command_line : image_name,
                                     resolved_image_name,
                                     sizeof(resolved_image_name),
                                     resolved_command_line,
                                     sizeof(resolved_command_line),
                                     &node,
                                     &bytes_read)) {
        return 0;
    }
    g_process_exec_last_stage = 3;
    if (!process_begin_elf_session()) {
        return 0;
    }
    process_set_name(&g_bound_session->process, resolved_image_name);
    g_process_exec_last_stage = 4;
    if (!process_load_elf_image(g_elf_file_buffer, bytes_read, &entry)) {
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    g_process_exec_last_stage = 5;
    if (!process_map_exec_stack()) {
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    g_process_exec_last_stage = 6;
    if (!process_prepare_arguments(resolved_command_line, envp, &stack_top)) {
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    g_process_exec_last_stage = 7;
    if (!session_enter_ring3(&g_user_session, g_user_page_mappings, entry, stack_top)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    g_process_exec_last_stage = 8;
    g_process_exec_last_error = PROCESS_EXEC_OK;
    return 1;
}

int process_run_ring3_smoke_test(void) {
    uint64_t entry = USER_ELF_BASE;

    g_process_exec_last_error = PROCESS_EXEC_OK;
    process_bind_session(&g_user_session, g_user_page_mappings);
    if (!process_begin_elf_session()) {
        return 0;
    }
    process_set_name(&g_bound_session->process, "ring3-smoke");
    if (g_bound_session->address_space.user_cr3 == 0 ||
        !vmm_root_is_current(g_bound_session->address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    if (!addrspace_map_range_with_perms(entry,
                                        entry + sizeof(g_ring3_smoke_code),
                                        VMM_PERM_WRITE | VMM_PERM_EXEC) ||
        !addrspace_copy_to_range(entry, g_ring3_smoke_code, sizeof(g_ring3_smoke_code)) ||
        !addrspace_map_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP)) {
        if (g_process_exec_last_error == PROCESS_EXEC_OK) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        }
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    if (!session_enter_ring3(&g_user_session, g_user_page_mappings, entry, USER_ELF_STACK_INIT)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    g_process_exec_last_error = PROCESS_EXEC_OK;
    return 1;
}

uint32_t process_last_error(void) {
    return g_process_exec_last_error;
}

int process_exec(struct vfs *vfs,
                 const char *name,
                 const char *const *envp,
                 enum process_exec_mode mode) {
    struct process_exec_request req;

    req.vfs = vfs;
    req.name = name;
    req.envp = envp;
    req.mode = mode;
    return process_dispatch_exec_request(&req);
}

int process_exec_from_user(struct vfs *vfs,
                           struct process *proc,
                           char *command_line,
                           const char *const *envp) {
    if (proc == 0 || command_line == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if (!process_resolve_exec_command_line(proc, command_line, NOS_TTY_LINE_BUFFER_SIZE)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    return process_run_foreground_command(vfs, proc, command_line, envp, PROCESS_EXEC_AUTO);
}

static int process_prepare_exec_replace_session_local(struct process_session *session,
                                                      struct user_page_mapping *mappings,
                                                      const struct process *proc) {
    struct process preserved;

    if (session == 0 || mappings == 0 || proc == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    preserved = *proc;
    process_bind_session(session, mappings);
    if (session->address_space.user_cr3 != 0 &&
        !vmm_root_is_current(session->address_space.user_cr3) &&
        !vmm_switch_root_or_fail(session->address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    addrspace_release_dynamic_pages();
    if (session->address_space.kernel_cr3 != 0 &&
        !vmm_switch_root_or_fail(session->address_space.kernel_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    session->address_space.kernel_cr3 = vmm_current_root();
    session->address_space.user_cr3 = vmm_create_user_root();
    if (session->address_space.user_cr3 == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    if (!vmm_switch_root_or_fail(session->address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    addrspace_unmap_range_if_present(USER_ELF_BASE, USER_ELF_LIMIT);
    addrspace_unmap_range_if_present(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    vmm_allow_user_range(USER_ELF_BASE, USER_ELF_LIMIT);
    vmm_allow_user_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);

    session->process = preserved;
    session->process.image_kind = PROCESS_IMAGE_ELF;
    session->process.address_space = &session->address_space;
    session->process.state = PROCESS_STATE_RUNNING;
    session->process.exit_code = 0;
    session->process.has_saved_frame = 0;
    session->process.wake_tick = 0;
    session->process.entry = 0;
    session->process.stack_top = 0;
    return 1;
}

int process_exec_replace_from_user(struct vfs *vfs,
                                   char *command_line,
                                   const char *const *envp) {
    struct process_session *session = process_current_session();
    struct user_page_mapping *mappings = process_current_mappings();
    struct process *proc = process_current_mut();
    char command_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_image_name[NOS_TTY_LINE_BUFFER_SIZE];
    char resolved_command_line[NOS_TTY_LINE_BUFFER_SIZE];
    uint32_t bytes_read = 0;
    uint64_t entry = 0;
    uint64_t stack_top = 0;

    g_process_exec_last_error = PROCESS_EXEC_OK;
    g_process_exec_last_stage = 1;
    g_process_exec_read_open_rc = 0;
    g_process_exec_read_node_kind = 0;
    g_process_exec_read_mount_kind = 0;
    g_process_exec_read_file_size = 0;
    g_process_exec_read_bytes = 0;
    g_process_exec_read_result = 0;
    if (vfs == 0 || proc == 0 || session == 0 || mappings == 0 || command_line == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if (!process_resolve_exec_command_line(proc, command_line, NOS_TTY_LINE_BUFFER_SIZE) ||
        !process_extract_command_name(command_line, command_name, sizeof(command_name))) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    g_process_exec_last_stage = 2;
    if (!process_resolve_exec_target(vfs,
                                     process_resolve_image_name(command_name),
                                     command_line,
                                     resolved_image_name,
                                     sizeof(resolved_image_name),
                                     resolved_command_line,
                                     sizeof(resolved_command_line),
                                     0,
                                     &bytes_read)) {
        return 0;
    }
    g_process_exec_last_stage = 3;
    if (!process_prepare_exec_replace_session_local(session, mappings, proc)) {
        session_finish(session, mappings);
        return 1;
    }
    process_set_name(&session->process, resolved_image_name);
    g_process_exec_last_stage = 4;
    if (!process_load_elf_image(g_elf_file_buffer, bytes_read, &entry)) {
        session_finish(session, mappings);
        return 1;
    }
    g_process_exec_last_stage = 5;
    if (!process_map_exec_stack()) {
        session_finish(session, mappings);
        return 1;
    }
    g_process_exec_last_stage = 6;
    if (!process_prepare_arguments(resolved_command_line, envp, &stack_top)) {
        session_finish(session, mappings);
        return 1;
    }
    g_process_exec_last_stage = 7;
    if (!session_enter_ring3(session, mappings, entry, stack_top)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        session_finish(session, mappings);
        return 1;
    }
    g_process_exec_last_stage = 8;
    g_process_exec_last_error = PROCESS_EXEC_OK;
    return 1;
}

int process_spawn_from_user(struct vfs *vfs,
                            struct process *proc,
                            char *command_line,
                            const char *const *envp,
                            uint32_t syscall_mode,
                            uint32_t flags) {
    enum process_exec_mode mode;

    if (proc == 0 || command_line == 0 || !process_map_spawn_mode_local(syscall_mode, &mode)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if (!process_resolve_exec_command_line(proc, command_line, NOS_TTY_LINE_BUFFER_SIZE)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }
    if ((flags & SYS_SPAWN_BACKGROUND) != 0) {
        if (mode != PROCESS_EXEC_ELF && mode != PROCESS_EXEC_AUTO) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
        return job_run_background_with_pid(vfs, command_line, envp, mode, 0);
    }
    return process_run_foreground_command(vfs, proc, command_line, envp, mode);
}

const struct process *process_current(void) {
    return g_bound_session->process.image_kind == PROCESS_IMAGE_NONE ? NULL : &g_bound_session->process;
}

struct process *process_current_mut(void) {
    return g_bound_session->process.image_kind == PROCESS_IMAGE_NONE ? NULL : &g_bound_session->process;
}
