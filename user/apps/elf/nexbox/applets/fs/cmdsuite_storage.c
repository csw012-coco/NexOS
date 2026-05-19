#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"

int cmd_mount(int argc, char **argv) {
    char source[CMD_PATH_MAX];
    char target[CMD_PATH_MAX];
    uint32_t kind = NEX_MOUNT_AUTO;
    uint32_t disk_index;
    uint32_t part_number;

    if (argc == 3) {
        copy_line_local(source, argv[1], sizeof(source));
        copy_line_local(target, argv[2], sizeof(target));
    } else if (argc == 4 && parse_mount_kind_local(argv[1], &kind)) {
        copy_line_local(source, argv[2], sizeof(source));
        copy_line_local(target, argv[3], sizeof(target));
    } else if (argc == 4 && parse_u32_local(argv[1], &disk_index) && parse_u32_local(argv[2], &part_number) &&
               part_number != 0) {
        if (snprintf(source, sizeof(source), "%s %s", argv[1], argv[2]) < 0) {
            source[0] = '\0';
        }
        copy_line_local(target, argv[3], sizeof(target));
    } else if (argc == 5 && parse_mount_kind_local(argv[1], &kind) &&
               parse_u32_local(argv[2], &disk_index) && parse_u32_local(argv[3], &part_number) &&
               part_number != 0) {
        if (snprintf(source, sizeof(source), "%s %s", argv[2], argv[3]) < 0) {
            source[0] = '\0';
        }
        copy_line_local(target, argv[4], sizeof(target));
    } else {
        write_err_str("usage: mount /dev/diskXpY /mnt\n");
        write_err_str("   or: mount boot /mnt\n");
        write_err_str("   or: mount [fat32|nxfs|auto] /dev/diskXpY /mnt\n");
        write_err_str("   or: mount [fat32|nxfs|auto] <disk> <part> /mnt\n");
        return 1;
    }

    {
        int rc = mount(source, target, kind);

        if (rc != 0) {
            write_err_str("mount failed: ");
            write_err_str(cmd_mount_error_message(rc));
            write_err_str(" (rc=");
            eprintf("%d)\n", rc);
            return 1;
        }
    }
    return 0;
}

int cmd_umount(int argc, char **argv) {
    int rc;

    if (argc != 2) {
        write_err_usage("umount", " <target>\n");
        return 1;
    }
    rc = umount(argv[1]);
    if (rc != 0) {
        write_err_str("umount failed: ");
        write_err_str(cmd_mount_error_message(rc));
        write_err_str(" (rc=");
        eprintf("%d)\n", rc);
        return 1;
    }
    return 0;
}

static int hotplug_mountpoint_local(uint32_t disk, uint32_t part_number, char *out, uint32_t out_size) {
    return out != NULL && out_size != 0u &&
           snprintf(out, out_size, "/media_disk%up%u", disk, part_number) >= 0;
}

static int hotplug_source_local(uint32_t disk, uint32_t part_number, char *out, uint32_t out_size) {
    return out != NULL && out_size != 0u &&
           snprintf(out, out_size, "/dev/disk%up%u", disk, part_number) >= 0;
}

static int hotplug_source_mounted_local(uint32_t disk, uint32_t part_index, char *target_out, uint32_t target_size) {
    struct syscall_mount_info info;

    for (uint32_t i = 0; mount_query(i, &info) > 0; i++) {
        if (info.source_known && info.disk_index == disk && info.part_index == part_index) {
            if (target_out != NULL && target_size != 0u) {
                if (snprintf(target_out, target_size, "/%s", info.target) < 0) {
                    target_out[0] = '\0';
                }
            }
            return 1;
        }
    }
    return 0;
}

static int hotplug_mount_partition_local(uint32_t disk, uint32_t part_index, int quiet) {
    char source[CMD_PATH_MAX];
    char target[CMD_PATH_MAX];
    char existing[CMD_PATH_MAX];
    uint32_t part_number = part_index + 1u;
    int rc;

    if (hotplug_source_mounted_local(disk, part_index, existing, sizeof(existing))) {
        if (!quiet) {
            write_str("mounted disk");
            write_dec(disk);
            write_str("p");
            write_dec(part_number);
            write_str(" ");
            write_str(existing);
            write_str(" already\n");
        }
        return 0;
    }
    if (!hotplug_source_local(disk, part_number, source, sizeof(source)) ||
        !hotplug_mountpoint_local(disk, part_number, target, sizeof(target))) {
        write_err_str("hotplug: path too long\n");
        return 1;
    }
    rc = mount(source, target, NEX_MOUNT_AUTO);
    if (rc != 0) {
        if (!quiet) {
            write_str("skip ");
            write_str(source);
            write_str(" -> ");
            write_str(target);
            write_str(": ");
            write_str(cmd_mount_error_message(rc));
            write_str("\n");
        }
        return 1;
    }
    write_str("mounted ");
    write_str(source);
    write_str(" -> ");
    write_str(target);
    write_str("\n");
    return 0;
}

static int hotplug_scan_local(int quiet) {
    struct syscall_block_info disk;
    struct syscall_partition_info part;
    uint32_t mounted = 0;
    uint32_t seen = 0;

    for (uint32_t d = 0; block_query(d, &disk) > 0; d++) {
        for (uint32_t p = 0; part_query(d, p, &part) > 0; p++) {
            seen++;
            if (hotplug_mount_partition_local(d, part.part_index, quiet) == 0) {
                mounted++;
            }
        }
    }
    if (!quiet) {
        write_str("hotplug: partitions=");
        write_dec(seen);
        write_str(" ready=");
        write_dec(mounted);
        write_str("\n");
    }
    return 0;
}

int cmd_hotplug(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && streq_ignore_case_local(argv[1], "scan"))) {
        return hotplug_scan_local(0);
    }
    if (argc == 2 && streq_ignore_case_local(argv[1], "list")) {
        return cmd_mounts();
    }
    if (argc == 4 && streq_ignore_case_local(argv[1], "mount")) {
        uint32_t disk;
        uint32_t part_number;

        if (!parse_u32_local(argv[2], &disk) || !parse_u32_local(argv[3], &part_number) ||
            part_number == 0u) {
            write_err_usage("hotplug", " mount <disk> <part>\n");
            return 1;
        }
        return hotplug_mount_partition_local(disk, part_number - 1u, 0);
    }
    if (argc == 2 && streq_ignore_case_local(argv[1], "watch")) {
        char *on_argv[] = {
            "on",
            "--daemon",
            "event.block.change",
            "op=partition",
            "run",
            "hotplug",
            "scan"
        };

        return cmd_on((int)(sizeof(on_argv) / sizeof(on_argv[0])), on_argv);
    }
    write_err_usage("hotplug", " [scan|list|watch|mount <disk> <part>]\n");
    return 1;
}

int cmd_switch_root(int argc, char **argv) {
    if (argc < 2) {
        write_err_usage("switch_root", " <path>\n");
        return 1;
    }
    if (switch_root(argv[1]) != 0) {
        write_err_str("switch_root failed\n");
        return 1;
    }
    return 0;
}
