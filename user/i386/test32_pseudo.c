#include "test32_pseudo.h"

static int test32_read_some(const char *path, char *buffer, size_t size, int code) {
    int fd = open(path, O_RDONLY);

    if (fd < 3 ||
        read(fd, buffer, size) <= 0 ||
        close(fd) != 0) {
        return code;
    }
    return 0;
}

static int test32_procfs_case(char *io_buffer, struct syscall_dirent *dirent) {
    uint32_t proc_root = 0u;
    int fd;
    int rc;

    rc = test32_read_some("/proc/drivers", io_buffer, 64u, 102);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/cpuinfo", io_buffer, 64u, 124);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/filesystems", io_buffer, 64u, 125);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/block", io_buffer, 64u, 126);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/partitions", io_buffer, 64u, 127);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/cmdline", io_buffer, 64u, 131);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/version", io_buffer, 64u, 132);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/fb", io_buffer, 64u, 133);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/interrupts", io_buffer, 64u, 134);
    if (rc != 0) return rc;
    rc = test32_read_some("/proc/tty", io_buffer, 64u, 135);
    if (rc != 0) return rc;

    fd = opendir("/proc");
    if (fd < 3) {
        return 128;
    }
    while (readdir((uint32_t)fd, dirent) > 0) {
        if (strcmp(dirent->name, "cpuinfo") == 0) {
            proc_root |= 1u;
        } else if (strcmp(dirent->name, "filesystems") == 0) {
            proc_root |= 2u;
        } else if (strcmp(dirent->name, "block") == 0) {
            proc_root |= 4u;
        } else if (strcmp(dirent->name, "partitions") == 0) {
            proc_root |= 8u;
        } else if (strcmp(dirent->name, "cmdline") == 0) {
            proc_root |= 16u;
        } else if (strcmp(dirent->name, "version") == 0) {
            proc_root |= 32u;
        } else if (strcmp(dirent->name, "fb") == 0) {
            proc_root |= 64u;
        } else if (strcmp(dirent->name, "interrupts") == 0) {
            proc_root |= 128u;
        } else if (strcmp(dirent->name, "tty") == 0) {
            proc_root |= 256u;
        }
    }
    if (close(fd) != 0 || proc_root != 0x1ffu) {
        return 129;
    }
    if (puts("[test32] libc32 procfs enriched files OK") == EOF) {
        return 130;
    }
    return 0;
}

static int test32_eventfs_case(char *io_buffer, struct syscall_dirent *dirent) {
    uint32_t event_root = 0u;
    uint32_t event_net = 0u;
    int fd;
    int rc;

    rc = test32_read_some("/event/timer", io_buffer, 64u, 103);
    if (rc != 0) return rc;

    fd = opendir("/event");
    if (fd < 3) {
        return 118;
    }
    while (readdir((uint32_t)fd, dirent) > 0) {
        if (strcmp(dirent->name, "timer") == 0) {
            event_root |= 1u;
        } else if (strcmp(dirent->name, "timer.json") == 0) {
            event_root |= 2u;
        } else if ((dirent->attributes & 0x10u) != 0u &&
                   strcmp(dirent->name, "input") == 0) {
            event_root |= 4u;
        } else if ((dirent->attributes & 0x10u) != 0u &&
                   strcmp(dirent->name, "net") == 0) {
            event_root |= 8u;
        } else if ((dirent->attributes & 0x10u) != 0u &&
                   strcmp(dirent->name, "file") == 0) {
            event_root |= 16u;
        } else if ((dirent->attributes & 0x10u) != 0u &&
                   strcmp(dirent->name, "block") == 0) {
            event_root |= 32u;
        } else if ((dirent->attributes & 0x10u) != 0u &&
                   strcmp(dirent->name, "security") == 0) {
            event_root |= 64u;
        }
    }
    if (close(fd) != 0 || event_root != 0x7fu) {
        return 119;
    }

    fd = opendir("/event/net");
    if (fd < 3) {
        return 120;
    }
    while (readdir((uint32_t)fd, dirent) > 0) {
        if (strcmp(dirent->name, "status") == 0) {
            event_net |= 1u;
        } else if (strcmp(dirent->name, "status.json") == 0) {
            event_net |= 2u;
        }
    }
    if (close(fd) != 0 || event_net != 3u) {
        return 121;
    }

    rc = test32_read_some("/event/input/keyboard", io_buffer, 64u, 147);
    if (rc != 0) return rc;
    rc = test32_read_some("/event/input/keyboard.json", io_buffer, 64u, 148);
    if (rc != 0) return rc;
    rc = test32_read_some("/event/input/mouse", io_buffer, 64u, 149);
    if (rc != 0) return rc;
    rc = test32_read_some("/event/input/mouse.json", io_buffer, 64u, 150);
    if (rc != 0) return rc;
    rc = test32_read_some("/event/timer.json", io_buffer, 64u, 122);
    if (rc != 0) return rc;

    if (puts("[test32] libc32 eventfs listing/json OK") == EOF) {
        return 123;
    }
    return 0;
}

static int test32_root_mountpoints_case(struct syscall_dirent *dirent) {
    uint32_t root_mounts = 0u;
    int fd;

    fd = opendir("/");
    if (fd < 3) {
        return 115;
    }
    while (readdir((uint32_t)fd, dirent) > 0) {
        if ((dirent->attributes & 0x10u) == 0u) {
            continue;
        }
        if (strcmp(dirent->name, "dev") == 0) {
            root_mounts |= 1u;
        } else if (strcmp(dirent->name, "proc") == 0) {
            root_mounts |= 2u;
        } else if (strcmp(dirent->name, "event") == 0) {
            root_mounts |= 4u;
        } else if (strcmp(dirent->name, "ram") == 0) {
            root_mounts |= 8u;
        }
    }
    if (close(fd) != 0 || root_mounts != 0x0fu) {
        return 116;
    }
    if (puts("[test32] libc32 root mountpoint readdir OK") == EOF) {
        return 117;
    }
    return 0;
}

int test32_pseudo_fs_case(void) {
    struct syscall_dirent dirent;
    char file_header[4];
    char io_buffer[64];
    int fd;
    int rc;

    fd = open("/dev/zero", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, 8u) != 8 ||
        io_buffer[0] != 0 ||
        io_buffer[7] != 0 ||
        close(fd) != 0) {
        return 101;
    }

    rc = test32_procfs_case(io_buffer, &dirent);
    if (rc != 0) return rc;
    rc = test32_eventfs_case(io_buffer, &dirent);
    if (rc != 0) return rc;

    fd = opendir("/cmd");
    if (fd < 3 ||
        readdir((uint32_t)fd, &dirent) <= 0 ||
        close(fd) != 0) {
        return 104;
    }
    rc = test32_root_mountpoints_case(&dirent);
    if (rc != 0) return rc;

    fd = open("/cmd/ls", O_RDONLY);
    if (fd < 3 ||
        read(fd, file_header, 2u) != 2 ||
        file_header[0] != '#' ||
        file_header[1] != '!' ||
        close(fd) != 0) {
        return 105;
    }
    if (puts("[test32] libc32 pseudo fs OK") == EOF) {
        return 106;
    }
    return 0;
}
