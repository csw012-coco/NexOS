#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static int path_is_dot_entry_local(const char *name) {
    return streq_local(name, ".") || streq_local(name, "..");
}

static const char *path_basename_local(const char *path) {
    const char *last = path;
    uint32_t i = 0;

    if (path == NULL || path[0] == '\0') {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            last = path + i + 1u;
        }
        i++;
    }
    return *last != '\0' ? last : path;
}

static int path_is_directory_local(const char *path) {
    int fd;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    fd = opendir(path);
    if (fd < 0) {
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static int path_join_local(char *out, uint32_t out_size, const char *dir, const char *name) {
    if (out == NULL || out_size == 0 || dir == NULL || name == NULL) {
        return 0;
    }
    if (snprintf(out,
                 out_size,
                 streq_local(dir, "/") ? "/%s" : "%s/%s",
                 name) < 0) {
        return 0;
    }
    return 1;
}

static int path_resolve_destination_local(const char *src, const char *dst, char *out, uint32_t out_size) {
    if (src == NULL || dst == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    if (path_is_directory_local(dst)) {
        return path_join_local(out, out_size, dst, path_basename_local(src));
    }
    copy_line_local(out, dst, out_size);
    return out[0] != '\0';
}

static int cmd_copy_file_local(const char *src, const char *dst) {
    char buf[64];
    char resolved_dst[CMD_PATH_MAX];
    int src_fd;
    int dst_fd;

    if (!path_resolve_destination_local(src, dst, resolved_dst, sizeof(resolved_dst))) {
        return -1;
    }
    src_fd = open(src, 0);
    if (src_fd < 0) {
        return -1;
    }
    dst_fd = open(resolved_dst, O_CREAT | O_TRUNC);
    if (dst_fd < 0) {
        close((uint32_t)src_fd);
        return -1;
    }
    for (;;) {
        uint32_t got = (uint32_t)read(src_fd, buf, sizeof(buf));
        uint32_t wrote;

        if (got == 0) {
            break;
        }
        wrote = (uint32_t)write(dst_fd, buf, got);
        if (wrote != got) {
            close((uint32_t)dst_fd);
            close((uint32_t)src_fd);
            return -1;
        }
        if (got < sizeof(buf)) {
            break;
        }
    }
    close((uint32_t)dst_fd);
    close((uint32_t)src_fd);
    return 0;
}

static int cmd_copy_recursive_local(const char *src, const char *dst) {
    struct syscall_dirent entry;
    char resolved_dst[CMD_PATH_MAX];
    char src_child[CMD_PATH_MAX];
    char dst_child[CMD_PATH_MAX];
    int src_fd;

    if (!path_resolve_destination_local(src, dst, resolved_dst, sizeof(resolved_dst))) {
        return -1;
    }
    src_fd = opendir(src);
    if (src_fd < 0) {
        return cmd_copy_file_local(src, dst);
    }
    if (mkdir(resolved_dst) != 0 && !path_is_directory_local(resolved_dst)) {
        close((uint32_t)src_fd);
        return -1;
    }
    while (readdir((uint32_t)src_fd, &entry) > 0) {
        if (path_is_dot_entry_local(entry.name)) {
            continue;
        }
        if (!path_join_local(src_child, sizeof(src_child), src, entry.name) ||
            !path_join_local(dst_child, sizeof(dst_child), resolved_dst, entry.name)) {
            close((uint32_t)src_fd);
            return -1;
        }
        if ((entry.attributes & 0x10u) != 0u) {
            if (cmd_copy_recursive_local(src_child, dst_child) != 0) {
                close((uint32_t)src_fd);
                return -1;
            }
        } else if (cmd_copy_file_local(src_child, dst_child) != 0) {
            close((uint32_t)src_fd);
            return -1;
        }
    }
    close((uint32_t)src_fd);
    return 0;
}

static int cmd_rm_recursive_local(const char *path, int force) {
    struct syscall_dirent entry;
    char child[CMD_PATH_MAX];
    int fd = opendir(path);

    if (fd < 0) {
        if (remove(path) == 0 || force) {
            return 0;
        }
        return -1;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (path_is_dot_entry_local(entry.name)) {
            continue;
        }
        if (snprintf(child,
                     sizeof(child),
                     streq_local(path, "/") ? "/%s" : "%s/%s",
                     entry.name) < 0) {
            close((uint32_t)fd);
            return -1;
        }
        if ((entry.attributes & 0x10u) != 0u) {
            if (cmd_rm_recursive_local(child, force) != 0) {
                close((uint32_t)fd);
                return -1;
            }
        } else if (remove(child) != 0 && !force) {
            close((uint32_t)fd);
            return -1;
        }
    }
    close((uint32_t)fd);
    if (rmdir(path) != 0 && !force) {
        return -1;
    }
    return 0;
}

int cmd_touch(int argc, char **argv) {
    int fd;

    if (argc < 2) {
        write_err_usage("touch", " <path>\n");
        return 1;
    }
    fd = open(argv[1], O_CREAT | O_APPEND);
    if (fd < 0) {
        write_err_str("touch: open failed\n");
        return 1;
    }
    close((uint32_t)fd);
    return 0;
}

int cmd_mv(int argc, char **argv) {
    char resolved_dst[CMD_PATH_MAX];

    if (argc < 3) {
        write_err_usage("mv", " <src> <dst>\n");
        return 1;
    }
    if (!path_resolve_destination_local(argv[1], argv[2], resolved_dst, sizeof(resolved_dst))) {
        write_err_str("mv: dest path invalid\n");
        return 1;
    }
    if (path_is_directory_local(argv[1])) {
        if (cmd_copy_recursive_local(argv[1], resolved_dst) != 0 ||
            cmd_rm_recursive_local(argv[1], 0) != 0) {
            write_err_str("mv: directory move failed\n");
            return 1;
        }
        return 0;
    }
    if (cmd_copy_file_local(argv[1], resolved_dst) != 0) {
        write_err_str("mv: file move failed\n");
        return 1;
    }
    if (remove(argv[1]) != 0) {
        write_err_str("mv: source remove failed\n");
        return 1;
    }
    return 0;
}

int cmd_cp(int argc, char **argv) {
    int recursive = 0;
    const char *src = 0;
    const char *dst = 0;
    int i;

    if (argc < 3) {
        write_err_usage("cp", " [-r] <src> <dst>\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (streq_local(argv[i], "-r")) {
            recursive = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            write_err_usage("cp", " [-r] <src> <dst>\n");
            return 1;
        } else if (src == 0) {
            src = argv[i];
        } else if (dst == 0) {
            dst = argv[i];
        } else {
            write_err_usage("cp", " [-r] <src> <dst>\n");
            return 1;
        }
    }
    if (src == 0 || dst == 0) {
        write_err_usage("cp", " [-r] <src> <dst>\n");
        return 1;
    }
    if (path_is_directory_local(src)) {
        if (!recursive) {
            write_err_str("cp: source is directory; use -r\n");
            return 1;
        }
        if (cmd_copy_recursive_local(src, dst) != 0) {
            write_err_str("cp: recursive copy failed\n");
            return 1;
        }
        return 0;
    }
    if (cmd_copy_file_local(src, dst) != 0) {
        write_err_str("cp: file copy failed\n");
        return 1;
    }
    return 0;
}

int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        write_err_usage("mkdir", " <path>\n");
        return 1;
    }
    if (mkdir(argv[1]) != 0) {
        write_err_str("mkdir failed\n");
        return 1;
    }
    return 0;
}

int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        write_err_usage("rmdir", " <path>\n");
        return 1;
    }
    if (rmdir(argv[1]) != 0) {
        write_err_str("rmdir failed\n");
        return 1;
    }
    return 0;
}

int cmd_rm(int argc, char **argv) {
    const char *path = 0;
    int recursive = 0;
    int force = 0;
    int i;

    if (argc < 2) {
        write_err_usage("rm", " [-r] [-f] <path>\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (streq_local(argv[i], "-r")) {
            recursive = 1;
        } else if (streq_local(argv[i], "-f")) {
            force = 1;
        } else if (streq_local(argv[i], "-rf") || streq_local(argv[i], "-fr")) {
            recursive = 1;
            force = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            write_err_usage("rm", " [-r] [-f] <path>\n");
            return 1;
        } else if (path == 0) {
            path = argv[i];
        } else {
            write_err_usage("rm", " [-r] [-f] <path>\n");
            return 1;
        }
    }
    if (path == 0 || path[0] == '\0') {
        write_err_usage("rm", " [-r] [-f] <path>\n");
        return 1;
    }
    if (recursive) {
        if (cmd_rm_recursive_local(path, force) != 0) {
            write_err_str("rm failed\n");
            return 1;
        }
        return 0;
    }
    if (remove(path) != 0 && !force) {
        write_err_str("rm failed\n");
        return 1;
    }
    return 0;
}

int cmd_run_like(int argc, char **argv, const char *verb, uint32_t mode, uint32_t flags, int use_exec) {
    char command[CMD_PATH_MAX];
    int rc;

    if (!cmd_build_program_command(argc, argv, 1, verb, streq_local(verb, "run"), command, sizeof(command))) {
        return 1;
    }
    rc = use_exec ? exec(command) : spawn(command, mode, flags);
    if (rc != 0) {
        write_err_str(verb);
        write_err_str(" failed rc=");
        eprintf("%d\n", rc);
        return 1;
    }
    return 0;
}
