#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"

static void write_mountpoints_for_source(uint32_t disk_index, uint32_t part_index) {
    struct syscall_mount_info info;
    int printed = 0;
    uint32_t i;

    for (i = 0; mount_query(i, &info) > 0; i++) {
        if (!info.source_known || info.disk_index != disk_index || info.part_index != part_index) {
            continue;
        }
        if (printed) {
            write_stdout(",", 1);
        }
        write_stdout("/", 1);
        write_str(info.target);
        printed = 1;
    }
}

int cmd_parts(int argc, char **argv) {
    struct syscall_partition_info info;
    uint32_t disk_index = 0;
    uint32_t i;

    if (argc > 1 && !parse_u32_local(argv[1], &disk_index)) {
        write_err_usage("parts", " [disk]\n");
        return 1;
    }
    write_str("disk");
    write_dec(disk_index);
    write_str("\npartitions\n");
    for (i = 0; part_query(disk_index, i, &info) > 0; i++) {
        write_str("#");
        write_dec(i);
        write_str(": slot=");
        write_dec(info.part_index + 1u);
        write_str(" type=0x");
        write_hex_u32(info.type);
        write_str(" lba=");
        storage_write_u64_dec(info.start_lba);
        write_str(" sectors=");
        storage_write_u64_dec(info.sector_count);
        write_str(" boot=");
        write_str(info.bootable ? "yes" : "no");
        write_str("\n");
    }
    return 0;
}

int cmd_blk(void) {
    struct syscall_block_info info;
    struct syscall_partition_info part;
    uint32_t i;

    write_str("NAME         MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS\n");
    for (i = 0; block_query(i, &info) > 0; i++) {
        uint32_t p;

        write_str("disk");
        write_dec(info.index);
        write_str(info.index < 10u ? "       " : "      ");
        write_dec(8u);
        write_str(":");
        write_dec(info.index * 16u);
        write_str(" 0 ");
        write_human_size(info.block_count * (uint64_t)info.block_size);
        write_str("  ");
        write_str(info.writable ? "0 " : "1 ");
        write_text_padded("disk", 4);
        write_str("\n");
        for (p = 0; part_query(i, p, &part) > 0; p++) {
            write_str((p + 1u == info.partition_count) ? "`-disk" : "|-disk");
            write_dec(info.index);
            write_str("p");
            write_dec(part.part_index + 1u);
            write_str((info.index < 10u && part.part_index < 9u) ? "    " : "   ");
            write_dec(8u);
            write_str(":");
            write_dec(info.index * 16u + part.part_index + 1u);
            write_str(" 0 ");
            write_human_size((uint64_t)part.sector_count * (uint64_t)info.block_size);
            write_str("  ");
            write_str(info.writable ? "0 " : "1 ");
            write_text_padded("part", 4);
            write_mountpoints_for_source(i, p);
            write_str("\n");
        }
    }
    return 0;
}

int cmd_mounts(void) {
    struct syscall_mount_info info;
    uint32_t i;

    write_str("mounts\n");
    for (i = 0; mount_query(i, &info) > 0; i++) {
        write_str("/");
        write_str(info.target);
        if (info.kind == NEX_MOUNT_INFO_DEVFS ||
            info.kind == NEX_MOUNT_INFO_PROCFS ||
            info.kind == NEX_MOUNT_INFO_EVENTFS) {
            write_str("\n");
            continue;
        }
        write_str(" -> ");
        write_str(info.kind == NEX_MOUNT_INFO_FAT32 ? "fat32 " : "nxfs ");
        if (info.source_known) {
            write_str("/dev/disk");
            write_dec(info.disk_index);
            if (info.part_index != 0xffffffffu) {
                write_str("p");
                write_dec(info.part_index + 1u);
            }
        } else {
            write_str("(source unknown)");
        }
        write_str("\n");
    }
    return 0;
}

static const char *df_mount_kind_name_local(uint32_t kind) {
    switch (kind) {
        case NEX_MOUNT_INFO_FAT32:
            return "fat32";
        case NEX_MOUNT_INFO_NXFS:
            return "nxfs";
        case NEX_MOUNT_INFO_DEVFS:
            return "devfs";
        case NEX_MOUNT_INFO_PROCFS:
            return "procfs";
        case NEX_MOUNT_INFO_EVENTFS:
            return "eventfs";
        default:
            return "unknown";
    }
}

static uint64_t df_mount_total_bytes_local(const struct syscall_mount_info *info) {
    return info->space_known ? info->total_blocks * (uint64_t)info->block_size : 0u;
}

static uint64_t df_mount_free_bytes_local(const struct syscall_mount_info *info) {
    return info->space_known ? info->free_blocks * (uint64_t)info->block_size : 0u;
}

static uint64_t df_mount_used_bytes_local(const struct syscall_mount_info *info) {
    uint64_t total = df_mount_total_bytes_local(info);
    uint64_t free = df_mount_free_bytes_local(info);

    return total > free ? total - free : 0u;
}

static uint32_t df_mount_use_percent_local(const struct syscall_mount_info *info) {
    uint64_t total = df_mount_total_bytes_local(info);
    uint64_t used = df_mount_used_bytes_local(info);

    if (total == 0u) {
        return 0u;
    }
    return (uint32_t)((used * 100u + total - 1u) / total);
}

static void df_write_mountpoint_local(const struct syscall_mount_info *info) {
    write_str("/");
    write_str(info->target);
}

static void df_write_quoted_mountpoint_local(const struct syscall_mount_info *info) {
    uint32_t i = 0;

    write_str("\"/");
    while (info->target[i] != '\0') {
        if (info->target[i] == '"' || info->target[i] == '\\') {
            char slash = '\\';
            write_stdout(&slash, 1);
        }
        write_stdout(&info->target[i], 1);
        i++;
    }
    write_str("\"");
}

static void df_write_source_local(const struct syscall_mount_info *info) {
    if (!info->source_known) {
        write_str("-");
        return;
    }
    write_str("/dev/disk");
    write_dec(info->disk_index);
    if (info->part_index != 0xffffffffu) {
        write_str("p");
        write_dec(info->part_index + 1u);
    }
}

static void df_write_human_or_dash_local(uint64_t bytes, int known) {
    if (!known) {
        write_str("-");
        return;
    }
    write_human_size(bytes);
}

int cmd_df(int argc, char **argv) {
    struct syscall_mount_info info;
    uint32_t i;
    int table = 0;

    if (argc == 2 && streq_local(argv[1], "--table")) {
        table = 1;
    } else if (argc != 1) {
        write_err_usage("df", " [--table]\n");
        return 1;
    }

    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: mount fs total used free use source\n");
    } else {
        write_str("Filesystem Size Used Avail Use% Mounted on Source\n");
    }

    for (i = 0; mount_query(i, &info) > 0; i++) {
        uint64_t total = df_mount_total_bytes_local(&info);
        uint64_t used = df_mount_used_bytes_local(&info);
        uint64_t free = df_mount_free_bytes_local(&info);
        uint32_t use_percent = df_mount_use_percent_local(&info);

        if (table) {
            df_write_quoted_mountpoint_local(&info);
            write_str(" ");
            write_str(df_mount_kind_name_local(info.kind));
            write_str(" ");
            storage_write_u64_dec(total);
            write_str(" ");
            storage_write_u64_dec(used);
            write_str(" ");
            storage_write_u64_dec(free);
            write_str(" ");
            write_dec(use_percent);
            write_str(" ");
            if (info.source_known) {
                write_str("\"/dev/disk");
                write_dec(info.disk_index);
                if (info.part_index != 0xffffffffu) {
                    write_str("p");
                    write_dec(info.part_index + 1u);
                }
                write_str("\"");
            } else {
                write_str("\"-\"");
            }
            write_str("\n");
            continue;
        }

        write_text_padded(df_mount_kind_name_local(info.kind), 10u);
        write_str(" ");
        df_write_human_or_dash_local(total, info.space_known);
        write_str(" ");
        df_write_human_or_dash_local(used, info.space_known);
        write_str(" ");
        df_write_human_or_dash_local(free, info.space_known);
        write_str(" ");
        if (info.space_known) {
            write_dec(use_percent);
            write_str("%");
        } else {
            write_str("-");
        }
        write_str(" ");
        df_write_mountpoint_local(&info);
        write_str(" ");
        df_write_source_local(&info);
        write_str("\n");
    }
    return 0;
}

int cmd_progs(void) {
    struct syscall_program_info info;
    uint32_t i;

    write_str("user programs\n");
    for (i = 0; program_query(i, &info) > 0; i++) {
        write_str("#");
        write_dec(i);
        write_str(": ");
        write_str(info.name);
        write_str("\n");
    }
    return 0;
}

int cmd_fatls(void) {
    struct syscall_root_entry_info info;
    uint32_t i;

    write_str("fat32 root\n");
    for (i = 0; root_query(i, &info) > 0; i++) {
        write_str(info.name);
        write_str(" cluster=");
        write_dec(info.native_id);
        write_str(" size=");
        write_dec(info.size);
        write_str("\n");
    }
    return 0;
}

int cmd_fatfind(int argc, char **argv) {
    struct syscall_root_entry_info info;

    if (argc < 2) {
        write_err_usage("fatfind", " <name>\n");
        return 1;
    }
    if (root_find(argv[1], &info) <= 0) {
        write_err_str("file not found\n");
        return 1;
    }
    write_str(info.name);
    write_str(" cluster=");
    write_dec(info.native_id);
    write_str(" size=");
    write_dec(info.size);
    write_str("\n");
    return 0;
}

int cmd_fatread(int argc, char **argv) {
    char path[CMD_PATH_MAX] = "/fat/";
    uint32_t len = str_len_local(path);

    if (argc < 2) {
        write_err_usage("fatread", " <name>\n");
        return 1;
    }
    if (len + str_len_local(argv[1]) + 1u >= sizeof(path)) {
        write_err_str("fatread: path too long\n");
        return 1;
    }
    copy_line_local(path + len, argv[1], sizeof(path) - len);
    {
        char *cat_argv[3];

        cat_argv[0] = "cat";
        cat_argv[1] = path;
        cat_argv[2] = NULL;
        return cmd_cat(2, cat_argv);
    }
}
