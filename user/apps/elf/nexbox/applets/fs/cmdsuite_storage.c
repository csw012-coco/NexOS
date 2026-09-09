#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"

#define CMD_FSTAB_PATH "/system/config/fstab.scf"
#define CMD_FSTAB_MAX_ENTRIES 16u
#define CMD_FSTAB_LINE_MAX 192u

struct cmd_fstab_entry {
    char source[CMD_PATH_MAX];
    char target[CMD_PATH_MAX];
    uint32_t kind;
    uint32_t order;
    uint8_t noauto;
    uint8_t nofail;
};

static int cmd_fstab_space(char ch) {
    return ch == ' ' || ch == '\t';
}

static const char *cmd_fstab_skip_space(const char *text) {
    while (text != NULL && cmd_fstab_space(*text)) {
        text++;
    }
    return text;
}

static int cmd_fstab_read_token(const char **cursor, char *out, uint32_t out_size) {
    const char *text;
    uint32_t count = 0u;

    if (cursor == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    text = cmd_fstab_skip_space(*cursor);
    if (text == NULL || *text == '\0' || *text == '#') {
        return 0;
    }
    while (*text != '\0' && !cmd_fstab_space(*text) && *text != '#') {
        if (count + 1u >= out_size) {
            return 0;
        }
        out[count++] = *text++;
    }
    out[count] = '\0';
    *cursor = text;
    return count != 0u;
}

static int cmd_fstab_parse_order(const char *line, const char **value_out, uint32_t *order_out) {
    const char *cursor = cmd_fstab_skip_space(line);
    uint32_t order = 0u;
    int digit_seen = 0;

    if (cursor == NULL || value_out == NULL || order_out == NULL ||
        cursor[0] == '#' || cursor[0] != 'm' || cursor[1] != 'o' ||
        cursor[2] != 'u' || cursor[3] != 'n' || cursor[4] != 't' ||
        cursor[5] != '.') {
        return 0;
    }
    cursor += 6;
    while (*cursor >= '0' && *cursor <= '9') {
        digit_seen = 1;
        order = order * 10u + (uint32_t)(*cursor - '0');
        cursor++;
    }
    if (!digit_seen || *cursor != '=') {
        return 0;
    }
    *value_out = cursor + 1;
    *order_out = order;
    return 1;
}

static int cmd_fstab_parse_options(const char *text, uint8_t *noauto_out, uint8_t *nofail_out) {
    const char *cursor = text;

    if (text == NULL || noauto_out == NULL || nofail_out == NULL) {
        return 0;
    }
    *noauto_out = 0u;
    *nofail_out = 0u;
    if (streq_local(text, "defaults")) {
        return 1;
    }
    while (*cursor != '\0') {
        char option[16];
        uint32_t count = 0u;

        while (*cursor != '\0' && *cursor != ',') {
            if (count + 1u >= sizeof(option)) {
                return 0;
            }
            option[count++] = *cursor++;
        }
        option[count] = '\0';
        if (streq_local(option, "nofail")) {
            *nofail_out = 1u;
        } else if (streq_local(option, "noauto")) {
            *noauto_out = 1u;
        } else {
            return 0;
        }
        if (*cursor == ',') {
            cursor++;
        }
    }
    return 1;
}

static int cmd_fstab_parse_entry(const char *line, struct cmd_fstab_entry *entry) {
    const char *value;
    const char *cursor;
    char kind[16];
    char options[32];

    if (entry == NULL || !cmd_fstab_parse_order(line, &value, &entry->order)) {
        return 0;
    }
    cursor = value;
    if (!cmd_fstab_read_token(&cursor, entry->source, sizeof(entry->source)) ||
        !cmd_fstab_read_token(&cursor, entry->target, sizeof(entry->target)) ||
        !cmd_fstab_read_token(&cursor, kind, sizeof(kind)) ||
        !cmd_fstab_read_token(&cursor, options, sizeof(options))) {
        return 0;
    }
    cursor = cmd_fstab_skip_space(cursor);
    return cursor != NULL && (*cursor == '\0' || *cursor == '#') &&
           parse_mount_kind_local(kind, &entry->kind) &&
           cmd_fstab_parse_options(options, &entry->noauto, &entry->nofail);
}

static int cmd_fstab_load(struct cmd_fstab_entry *entries, uint32_t *count_out) {
    char line[CMD_FSTAB_LINE_MAX];
    uint32_t count = 0u;
    int fd;

    if (entries == NULL || count_out == NULL) {
        return 0;
    }
    fd = open(CMD_FSTAB_PATH, O_RDONLY);
    if (fd < 0) {
        fd = open("/fstab.scf", O_RDONLY);
    }
    if (fd < 0) {
        write_err_str("mount: cannot open " CMD_FSTAB_PATH "\n");
        return 0;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        struct cmd_fstab_entry entry;
        const char *value;
        uint32_t order;

        if (!cmd_fstab_parse_order(line, &value, &order)) {
            continue;
        }
        (void)value;
        (void)order;
        if (count >= CMD_FSTAB_MAX_ENTRIES || !cmd_fstab_parse_entry(line, &entry)) {
            close((uint32_t)fd);
            write_err_str("mount: invalid fstab entry\n");
            return 0;
        }
        entries[count++] = entry;
    }
    close((uint32_t)fd);
    *count_out = count;
    return 1;
}

static void cmd_fstab_sort(struct cmd_fstab_entry *entries, uint32_t count) {
    for (uint32_t i = 1u; i < count; i++) {
        struct cmd_fstab_entry entry = entries[i];
        uint32_t pos = i;

        while (pos != 0u && entries[pos - 1u].order > entry.order) {
            entries[pos] = entries[pos - 1u];
            pos--;
        }
        entries[pos] = entry;
    }
}

static int cmd_fstab_target_exists(const char *target) {
    struct syscall_mount_info info;
    const char *name = target;

    if (name != NULL && name[0] == '/') {
        name++;
    }
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (uint32_t i = 0u; mount_query(i, &info) > 0; i++) {
        if (streq_local(info.target, name)) {
            return 1;
        }
    }
    return 0;
}

static int cmd_mount_all(void) {
    struct cmd_fstab_entry entries[CMD_FSTAB_MAX_ENTRIES];
    uint32_t count = 0u;
    int status = 0;

    if (!cmd_fstab_load(entries, &count)) {
        return 1;
    }
    cmd_fstab_sort(entries, count);
    for (uint32_t i = 0u; i < count; i++) {
        int rc;

        if (entries[i].noauto) {
            continue;
        }
        if (streq_local(entries[i].source, "root")) {
            rc = switch_root("/dev/root");
        } else if (streq_local(entries[i].source, "ram")) {
            /* The ramdisk is the bootstrap root and is mounted by the kernel. */
            if (cmd_fstab_target_exists(entries[i].target)) {
                continue;
            }
            rc = -NEX_ERR_NOENT;
        } else if (streq_local(entries[i].source, "boot") &&
                   cmd_fstab_target_exists(entries[i].target)) {
            continue;
        } else {
            rc = mount(entries[i].source, entries[i].target, entries[i].kind);
        }
        if (rc != 0 && !entries[i].nofail) {
            write_err_str("mount: ");
            write_err_str(entries[i].target);
            write_err_str(": ");
            write_err_str(cmd_mount_error_message(rc));
            write_err_str("\n");
            status = 1;
        }
    }
    return status;
}

int cmd_mount(int argc, char **argv) {
    char source[CMD_PATH_MAX];
    char target[CMD_PATH_MAX];
    uint32_t kind = NEX_MOUNT_AUTO;
    uint32_t disk_index;
    uint32_t part_number;

    if (argc == 2 && streq_local(argv[1], "-a")) {
        return cmd_mount_all();
    }
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
        write_err_str("usage: mount -a\n");
        write_err_str("usage: mount /dev/diskXpY /mnt\n");
        write_err_str("   or: mount boot /mnt\n");
        write_err_str("   or: mount [fat32|nxfs|auto] /dev/diskXpY /mnt\n");
        write_err_str("   or: mount [fat32|nxfs|auto] <disk> <part> /mnt\n");
        return 1;
    }

    {
        int rc = mount(source, target, kind);

        if (rc != 0) {
            if (rc == -NEX_ERR_ACCES || rc == -NEX_ERR_PERM) {
                return cmd_report_access_denied(
                    "mount",
                    "requires mount capability");
            }
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
        if (rc == -NEX_ERR_ACCES || rc == -NEX_ERR_PERM) {
            return cmd_report_access_denied(
                "umount",
                "requires mount capability");
        }
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
    {
        int rc = switch_root(argv[1]);

        if (rc != 0) {
            return cmd_report_syscall_failure(
                "switch_root",
                "requires mount capability",
                rc);
        }
    }
    return 0;
}
