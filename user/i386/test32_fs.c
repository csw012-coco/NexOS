#include "test32_fs.h"

static int test32_fs_find_mount(const char *target,
                                uint32_t kind,
                                struct syscall_mount_info *out) {
    struct syscall_mount_info candidate;

    if (target == 0 || out == 0) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < 4u; attempt++) {
        for (uint32_t i = 0u; i < NOS_MOUNT_SLOT_MAX + 2u; i++) {
            if (mount_query(i, &candidate) > 0 &&
                candidate.kind == kind &&
                strcmp(candidate.target, target) == 0) {
                *out = candidate;
                return 1;
            }
        }
        yield();
    }
    return 0;
}

int test32_fs_open_read_close_case(void) {
    char file_header[4];
    struct syscall_fd_info fd_info;
    int fd;

    fd = open("/cmd/test32", O_RDONLY);
    if (fd < 3 ||
        fd_query((uint32_t)fd, &fd_info) <= 0 ||
        fd_info.kind != SYS_FD_KIND_VFS ||
        fd_info.readable != 1u ||
        fd_info.writable != 0u ||
        strcmp(fd_info.path, "/cmd/test32") != 0 ||
        read(fd, file_header, sizeof(file_header)) !=
            (ssize_t)sizeof(file_header) ||
        memcmp(file_header, "\x7f" "ELF", sizeof(file_header)) != 0 ||
        fd_query((uint32_t)fd, &fd_info) <= 0 ||
        fd_info.offset != sizeof(file_header) ||
        close(fd) != 0 ||
        close(fd) == 0) {
        return 25;
    }
    if (puts("[test32] libc32 open/read/close VFS OK") == EOF) {
        return 26;
    }
    return 0;
}

int test32_fs_create_truncate_case(void) {
    char io_buffer[64];
    int fd;

    fd = open("/REDIR32.TXT", O_CREAT | O_TRUNC);
    if (fd < 3 ||
        write(fd, "ok\n", 3u) != 3 ||
        close(fd) != 0) {
        return 112;
    }
    fd = open("/REDIR32.TXT", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, 3u) != 3 ||
        memcmp(io_buffer, "ok\n", 3u) != 0 ||
        close(fd) != 0 ||
        remove("/REDIR32.TXT") != 0) {
        return 113;
    }
    if (puts("[test32] libc32 create/truncate default-write open OK") == EOF) {
        return 114;
    }
    return 0;
}

int test32_fs_mkdir_remove_rmdir_case(pid_t pid) {
    char dir_path[32];
    char remove_path[40];
    FILE *stream;

    if (snprintf(dir_path,
                 sizeof(dir_path),
                 "/ram/DIR%u",
                 (uint32_t)pid) <= 0 ||
        snprintf(remove_path,
                 sizeof(remove_path),
                 "%s/RM.TXT",
                 dir_path) <= 0) {
        return 60;
    }
    if (mkdir(dir_path) != 0) {
        return 61;
    }
    stream = fopen(remove_path, "w");
    if (stream == 0 ||
        fwrite("remove\n", 1u, 7u, stream) != 7u ||
        fclose(stream) != 0) {
        return 62;
    }
    if (remove(remove_path) != 0 ||
        fopen(remove_path, "r") != 0) {
        return 63;
    }
    if (rmdir(dir_path) != 0 ||
        opendir(dir_path) >= 0) {
        return 64;
    }
    if (puts("[test32] libc32 mkdir/remove/rmdir syscalls OK") == EOF) {
        return 65;
    }
    return 0;
}

static int test32_fs_fat32_lfn_case(const char *mount_path) {
    char lfn_path[80];
    char io_buffer[32];
    int fd;

    if (snprintf(lfn_path,
                 sizeof(lfn_path),
                 "%s/Long File Name 32.txt",
                 mount_path) <= 0) {
        return 87;
    }
    fd = open(lfn_path, O_CREAT | O_TRUNC);
    if (fd < 3 ||
        write(fd, "lfn32\n", 6u) != 6 ||
        close(fd) != 0) {
        return 89;
    }
    fd = open(lfn_path, O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, 6u) != 6 ||
        memcmp(io_buffer, "lfn32\n", 6u) != 0 ||
        close(fd) != 0 ||
        remove(lfn_path) != 0 ||
        open(lfn_path, O_RDONLY) >= 0) {
        return 90;
    }
    return 0;
}

int test32_fs_mount_umount_case(pid_t pid) {
    char file_header[4];
    char mount_path[32];
    char mounted_file[64];
    struct syscall_mount_info mount_info;
    int rc;
    int fd;

    if (snprintf(mount_path,
                 sizeof(mount_path),
                 "/MNT%u",
                 (uint32_t)pid) <= 0 ||
        snprintf(mounted_file,
                 sizeof(mounted_file),
                 "%s/BOOT/TEST32.ELF",
                 mount_path) <= 0) {
        return 71;
    }
    if (mkdir(mount_path) != 0 ||
        mount("/dev/disk0p1", mount_path, SYS_MOUNT_AUTO) != 0) {
        return 72;
    }
    if (!test32_fs_find_mount(mount_path + 1,
                              SYS_MOUNT_INFO_FAT32,
                              &mount_info) ||
        mount_info.disk_index != 0u ||
        mount_info.part_index != 0u ||
        mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 84;
    }
    fd = open(mounted_file, O_RDONLY);
    if (fd < 3 ||
        read(fd, file_header, sizeof(file_header)) !=
            (ssize_t)sizeof(file_header) ||
        memcmp(file_header, "\x7f" "ELF", sizeof(file_header)) != 0 ||
        close(fd) != 0) {
        return 73;
    }
    rc = test32_fs_fat32_lfn_case(mount_path);
    if (rc != 0) {
        return rc;
    }
    if (umount(mount_path) != 0 ||
        rmdir(mount_path) != 0) {
        return 74;
    }
    if (puts("[test32] libc32 mount/umount syscalls OK") == EOF) {
        return 75;
    }
    return 0;
}

int test32_fs_nxfs_mount_case(pid_t pid) {
    char nxfs_mount_path[32];
    struct syscall_mount_info mount_info;
    int fd;

    if (snprintf(nxfs_mount_path,
                 sizeof(nxfs_mount_path),
                 "/NX%u",
                 (uint32_t)pid) <= 0) {
        return 76;
    }
    if (mkdir(nxfs_mount_path) != 0 ||
        mount("/dev/disk1p1", nxfs_mount_path, SYS_MOUNT_NXFS) != 0) {
        return 77;
    }
    if (!test32_fs_find_mount(nxfs_mount_path + 1,
                              SYS_MOUNT_INFO_NXFS,
                              &mount_info)) {
        return 83;
    }
    if (mount_info.disk_index != 1u ||
        mount_info.part_index != 0u) {
        return 86;
    }
    if (mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 88;
    }
    fd = opendir(nxfs_mount_path);
    if (fd < 3 ||
        close(fd) != 0) {
        return 78;
    }
    if (umount(nxfs_mount_path) != 0 ||
        rmdir(nxfs_mount_path) != 0) {
        return 79;
    }
    if (puts("[test32] libc32 nxfs mount syscall OK") == EOF) {
        return 80;
    }
    return 0;
}
