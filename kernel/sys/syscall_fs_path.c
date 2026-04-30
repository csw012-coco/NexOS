#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/fs/fs_service_path_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "fs/vfs.h"

static uint32_t syscall_path_len(const char *text) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static void syscall_copy_path(char *dst, uint32_t dst_size, const char *src) {
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

static uint32_t syscall_find_last_slash(const char *text) {
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

static void syscall_path_pop_segment(char *path) {
    uint32_t len;
    uint32_t last;

    if (path == 0) {
        return;
    }
    len = syscall_path_len(path);
    if (len <= 1u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    last = syscall_find_last_slash(path);
    if (last == 0u) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    path[last] = '\0';
}

static int syscall_path_append_segment(char *path, uint32_t path_size, const char *segment, uint32_t seg_len) {
    uint32_t len;
    uint32_t i;

    if (path == 0 || segment == 0 || seg_len == 0) {
        return 0;
    }
    len = syscall_path_len(path);
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

static int syscall_resolve_process_path(const struct process *proc,
                                        const char *input,
                                        char *out,
                                        uint32_t out_size) {
    uint32_t pos = 0;

    if (input == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
    } else {
        syscall_copy_path(out, out_size, process_cwd(proc));
    }
    if (input[0] == '\0' || streq(input, ".")) {
        return 1;
    }
    if (input[0] == '/') {
        pos = 1u;
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
            syscall_path_pop_segment(out);
            continue;
        }
        if (!syscall_path_append_segment(out, out_size, segment, seg_len)) {
            return 0;
        }
    }
    return 1;
}

static int syscall_copy_resolved_user_path(struct process *proc, uint64_t user_path_addr, char *buffer, uint32_t size) {
    char input[SYSCALL_PATH_MAX + 1];

    if (!syscall_copy_user_cstr(buffer, user_path_addr, size)) {
        return -1;
    }
    syscall_copy_path(input, sizeof(input), buffer);
    if (!syscall_resolve_process_path(proc, input, buffer, size)) {
        return 0;
    }
    return 1;
}

static uint32_t syscall_append_u32(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    char digits[10];
    uint32_t count = 0;

    if (dst == 0 || dst_size == 0 || pos >= dst_size) {
        return pos;
    }
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && count < sizeof(digits));
    while (count > 0 && pos + 1u < dst_size) {
        dst[pos++] = digits[--count];
    }
    dst[pos] = '\0';
    return pos;
}

static int syscall_mount_source_is_boot(const char *source) {
    return streq(source, "boot") || streq(source, "/dev/boot");
}

static int syscall_resolve_boot_mount_source(char *source, uint32_t source_size) {
    uint32_t disk_index;
    uint32_t part_index;
    uint32_t pos = 0;

    if (source == 0 || source_size == 0 || !syscall_mount_source_is_boot(source)) {
        return 1;
    }
    if (g_syscall_boot_info == 0 ||
        !vfs_find_source_by_boot_partition(g_syscall_boot_info->partition_lba,
                                           g_syscall_boot_info->partition_sectors,
                                           &disk_index,
                                           &part_index)) {
        return 0;
    }
    if (part_index == VFS_PARTITION_RAW) {
        if (source_size < 10u) {
            return 0;
        }
        source[0] = '/';
        source[1] = 'd';
        source[2] = 'e';
        source[3] = 'v';
        source[4] = '/';
        source[5] = 'd';
        source[6] = 'i';
        source[7] = 's';
        source[8] = 'k';
        source[9] = '\0';
        (void)syscall_append_u32(source, 9u, source_size, disk_index);
        return 1;
    }
    pos = syscall_append_u32(source, pos, source_size, disk_index);
    if (pos + 2u >= source_size) {
        return 0;
    }
    source[pos++] = ' ';
    source[pos] = '\0';
    (void)syscall_append_u32(source, pos, source_size, part_index + 1u);
    return 1;
}

uint64_t syscall_handle_mkdir(uint64_t user_path_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_path_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_mkdir(g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_rmdir(uint64_t user_path_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_path_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_rmdir(g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_remove(uint64_t user_path_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_path_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_remove(g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_mount(uint64_t user_source_addr, uint64_t user_target_addr, uint32_t syscall_kind) {
    int boot_source;

    if (g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_path_buffer, user_source_addr, sizeof(g_syscall_path_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    boot_source = syscall_mount_source_is_boot(g_syscall_path_buffer);
    if (!boot_source && !syscall_resolve_boot_mount_source(g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        return (uint64_t)(-(int64_t)SYS_MOUNT_ERR_INVALID_SOURCE);
    }
    switch (syscall_copy_resolved_user_path(process_current_mut(),
                                            user_target_addr,
                                            g_syscall_path_buffer2,
                                            sizeof(g_syscall_path_buffer2))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    if (boot_source) {
        if (g_syscall_boot_info == 0) {
            return (uint64_t)(-(int64_t)SYS_MOUNT_ERR_INVALID_SOURCE);
        }
        return fs_service_mount_boot(g_syscall_vfs,
                                     g_syscall_path_buffer2,
                                     syscall_kind,
                                     g_syscall_boot_info->partition_lba,
                                     g_syscall_boot_info->partition_sectors);
    }
    return fs_service_mount(g_syscall_vfs, g_syscall_path_buffer, g_syscall_path_buffer2, syscall_kind);
}

uint64_t syscall_handle_umount(uint64_t user_target_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_target_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_umount(g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_switch_root(uint64_t user_target_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_target_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_switch_root(g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_open(uint64_t user_name_addr, uint32_t flags) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_name_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_open(proc, g_syscall_vfs, g_syscall_path_buffer, flags);
}

uint64_t syscall_handle_opendir(uint64_t user_path_addr) {
    struct process *proc = process_current_mut();

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_path_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    return fs_service_opendir(proc, g_syscall_vfs, g_syscall_path_buffer);
}

uint64_t syscall_handle_chdir(uint64_t user_path_addr) {
    struct process *proc = process_current_mut();
    uint64_t fd;

    if (proc == 0 || g_syscall_vfs == 0) {
        return (uint64_t)-1;
    }
    switch (syscall_copy_resolved_user_path(proc, user_path_addr, g_syscall_path_buffer, sizeof(g_syscall_path_buffer))) {
        case -1:
            return syscall_kill_bad_user_pointer();
        case 0:
            return (uint64_t)-1;
        default:
            break;
    }
    fd = fs_service_opendir(proc, g_syscall_vfs, g_syscall_path_buffer);
    if ((int64_t)fd < 0) {
        return (uint64_t)-1;
    }
    (void)fs_service_close(proc, (uint32_t)fd);
    process_set_cwd(proc, g_syscall_path_buffer);
    return 0;
}

uint64_t syscall_handle_getcwd(uint64_t user_path_addr, uint32_t size) {
    struct process *proc = process_current_mut();
    const char *cwd;
    uint32_t len;

    if (proc == 0 || user_path_addr == 0 || size == 0) {
        return (uint64_t)-1;
    }
    cwd = process_cwd(proc);
    len = syscall_path_len(cwd) + 1u;
    if (len > size || !syscall_user_writable(user_path_addr, size)) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_to_user(user_path_addr, cwd, len)) {
        return syscall_kill_bad_user_pointer();
    }
    return len - 1u;
}
