#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

enum {
    FDISK_MBR_BLOCK_SIZE = 512u,
    FDISK_MBR_TABLE_OFFSET = 446u,
    FDISK_MBR_ENTRY_SIZE = 16u,
    FDISK_MBR_SLOT_COUNT = 4u
};

static uint32_t fdisk_le32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void fdisk_put_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint8_t *fdisk_slot_ptr(uint8_t *mbr, uint32_t slot) {
    return mbr + FDISK_MBR_TABLE_OFFSET + slot * FDISK_MBR_ENTRY_SIZE;
}

static const char *fdisk_partition_type_name(uint32_t type) {
    switch (type & 0xffu) {
        case 0x00:
            return "empty";
        case 0x01:
            return "fat12";
        case 0x04:
            return "fat16<32m";
        case 0x06:
            return "fat16";
        case 0x07:
            return "hpfs/ntfs/exfat";
        case 0x0b:
            return "fat32";
        case 0x0c:
            return "fat32-lba";
        case 0x0e:
            return "fat16-lba";
        case 0x0f:
            return "extended-lba";
        case 0x05:
            return "extended";
        case 0x82:
            return "swap";
        case 0x83:
            return "linux";
        case 0xa5:
            return "bsd";
        case 0xab:
            return "osx-boot";
        case 0xaf:
            return "hfs+";
        default:
            return "unknown";
    }
}

static int fdisk_parse_slot_local(const char *text, uint32_t *slot_out) {
    uint32_t value;

    if (!parse_u32_local(text, &value) || value == 0u || value > FDISK_MBR_SLOT_COUNT || slot_out == NULL) {
        return 0;
    }
    *slot_out = value - 1u;
    return 1;
}

static int fdisk_parse_type_local(const char *text, uint32_t *type_out) {
    char *end = 0;
    unsigned long value;

    if (text == NULL || text[0] == '\0' || type_out == NULL) {
        return 0;
    }
    value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0xfful) {
        return 0;
    }
    *type_out = (uint32_t)value;
    return 1;
}

static int fdisk_load_mbr(uint32_t disk_index,
                          struct syscall_block_info *block_info,
                          struct syscall_block_read_info *read_info) {
    if (block_info == NULL || read_info == NULL) {
        return 0;
    }
    if (block_query(disk_index, block_info) <= 0) {
        write_err_str("fdisk: disk not found\n");
        return 0;
    }
    if (block_info->block_size != FDISK_MBR_BLOCK_SIZE) {
        write_err_str("fdisk: only 512-byte block disks are supported\n");
        return 0;
    }
    if (block_read(disk_index, 0, read_info) <= 0 || read_info->bytes_read != FDISK_MBR_BLOCK_SIZE) {
        write_err_str("fdisk: mbr read failed\n");
        return 0;
    }
    return 1;
}

static int fdisk_store_mbr(uint32_t disk_index, const uint8_t *mbr) {
    struct syscall_block_write_info info;
    uint32_t i;

    info.disk_index = disk_index;
    info.block_size = FDISK_MBR_BLOCK_SIZE;
    info.bytes_to_write = FDISK_MBR_BLOCK_SIZE;
    info.bytes_written = 0;
    info.lba = 0;
    for (i = 0; i < FDISK_MBR_BLOCK_SIZE; i++) {
        info.data[i] = mbr[i];
    }
    return block_write(disk_index, 0, &info) > 0 && info.bytes_written == FDISK_MBR_BLOCK_SIZE;
}

static void fdisk_print_summary_line(const struct syscall_block_info *info) {
    write_str("disk");
    write_dec(info->index);
    write_str(": ");
    write_str(info->name);
    write_str(" size=");
    write_human_size(info->block_count * (uint64_t)info->block_size);
    write_str(" block=");
    write_dec(info->block_size);
    write_str(" writable=");
    write_str(info->writable ? "yes" : "no");
    write_str(" partitions=");
    write_dec(info->partition_count);
    write_str("\n");
}

static void fdisk_print_slot(const uint8_t *entry, uint32_t slot) {
    uint32_t type = entry[4];
    uint32_t start_lba = fdisk_le32(entry + 8);
    uint32_t sector_count = fdisk_le32(entry + 12);

    write_str("slot ");
    write_dec(slot + 1u);
    write_str(": ");
    if (type == 0u || sector_count == 0u) {
        write_str("<empty>\n");
        return;
    }
    write_str((entry[0] & 0x80u) != 0u ? "boot " : "     ");
    write_str("type=0x");
    write_hex_u32(type);
    write_str(" (");
    write_str(fdisk_partition_type_name(type));
    write_str(") lba=");
    write_dec(start_lba);
    write_str(" sectors=");
    write_dec(sector_count);
    write_str(" end=");
    write_dec(start_lba + sector_count - 1u);
    write_str("\n");
}

static int fdisk_show_disk(uint32_t disk_index) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t disk_signature;
    uint32_t slot;
    int valid_signature;

    if (!fdisk_load_mbr(disk_index, &block_info, &read_info)) {
        return 1;
    }
    valid_signature = read_info.data[510] == 0x55u && read_info.data[511] == 0xaau;
    disk_signature = fdisk_le32(read_info.data + 440);
    fdisk_print_summary_line(&block_info);
    write_str("mbr signature=");
    write_str(valid_signature ? "valid" : "missing");
    write_str(" disk_id=0x");
    write_hex_u32(disk_signature);
    write_str("\n");
    for (slot = 0; slot < FDISK_MBR_SLOT_COUNT; slot++) {
        fdisk_print_slot(fdisk_slot_ptr(read_info.data, slot), slot);
    }
    return 0;
}

static void fdisk_zero_slot(uint8_t *entry) {
    for (uint32_t i = 0; i < FDISK_MBR_ENTRY_SIZE; i++) {
        entry[i] = 0;
    }
}

static int fdisk_update_signature(uint8_t *mbr) {
    if (mbr == NULL) {
        return 0;
    }
    mbr[510] = 0x55u;
    mbr[511] = 0xaau;
    return 1;
}

static int fdisk_command_set(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t slot;
    uint32_t start_lba;
    uint32_t sector_count;
    uint32_t type;
    uint8_t *entry;
    uint32_t i;
    int bootable = 0;

    if (argc != 7 && argc != 8) {
        write_err_usage("fdisk", " <disk> set <slot 1..4> <start_lba> <sectors> <type> [boot]\n");
        return 1;
    }
    if (!fdisk_parse_slot_local(argv[3], &slot) || !parse_u32_local(argv[4], &start_lba) ||
        !parse_u32_local(argv[5], &sector_count) || !fdisk_parse_type_local(argv[6], &type)) {
        write_err_str("fdisk: invalid numeric argument\n");
        return 1;
    }
    if (sector_count == 0u || type == 0u) {
        write_err_str("fdisk: use non-zero sector count and type; use clear for empty slots\n");
        return 1;
    }
    if (argc == 8) {
        if (!streq_ignore_case_local(argv[7], "boot") && !streq_ignore_case_local(argv[7], "active")) {
            write_err_str("fdisk: final argument must be 'boot' or 'active'\n");
            return 1;
        }
        bootable = 1;
    }
    if (!fdisk_load_mbr(disk_index, &block_info, &read_info)) {
        return 1;
    }
    if (!block_info.writable) {
        write_err_str("fdisk: disk is read-only\n");
        return 1;
    }
    entry = fdisk_slot_ptr(read_info.data, slot);
    fdisk_zero_slot(entry);
    entry[0] = bootable ? 0x80u : 0x00u;
    entry[1] = 0xfeu;
    entry[2] = 0xffu;
    entry[3] = 0xffu;
    entry[4] = (uint8_t)type;
    entry[5] = 0xfeu;
    entry[6] = 0xffu;
    entry[7] = 0xffu;
    fdisk_put_le32(entry + 8, start_lba);
    fdisk_put_le32(entry + 12, sector_count);
    if (bootable) {
        for (i = 0; i < FDISK_MBR_SLOT_COUNT; i++) {
            if (i == slot) {
                continue;
            }
            fdisk_slot_ptr(read_info.data, i)[0] = 0x00u;
        }
    }
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: updated disk");
    write_dec(disk_index);
    write_str(" slot ");
    write_dec(slot + 1u);
    write_str("\n");
    return 0;
}

static int fdisk_command_clear(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t slot;

    if (argc != 4 || !fdisk_parse_slot_local(argv[3], &slot)) {
        write_err_usage("fdisk", " <disk> clear <slot 1..4>\n");
        return 1;
    }
    if (!fdisk_load_mbr(disk_index, &block_info, &read_info)) {
        return 1;
    }
    if (!block_info.writable) {
        write_err_str("fdisk: disk is read-only\n");
        return 1;
    }
    fdisk_zero_slot(fdisk_slot_ptr(read_info.data, slot));
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: cleared disk");
    write_dec(disk_index);
    write_str(" slot ");
    write_dec(slot + 1u);
    write_str("\n");
    return 0;
}

static int fdisk_command_boot(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t slot;
    uint32_t i;
    uint8_t *entry;

    if (argc != 4 || !fdisk_parse_slot_local(argv[3], &slot)) {
        write_err_usage("fdisk", " <disk> boot <slot 1..4>\n");
        return 1;
    }
    if (!fdisk_load_mbr(disk_index, &block_info, &read_info)) {
        return 1;
    }
    if (!block_info.writable) {
        write_err_str("fdisk: disk is read-only\n");
        return 1;
    }
    entry = fdisk_slot_ptr(read_info.data, slot);
    if (entry[4] == 0u || fdisk_le32(entry + 12) == 0u) {
        write_err_str("fdisk: selected slot is empty\n");
        return 1;
    }
    for (i = 0; i < FDISK_MBR_SLOT_COUNT; i++) {
        fdisk_slot_ptr(read_info.data, i)[0] = i == slot ? 0x80u : 0x00u;
    }
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: disk");
    write_dec(disk_index);
    write_str(" active slot ");
    write_dec(slot + 1u);
    write_str("\n");
    return 0;
}

static int fdisk_command_wipe(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t slot;

    (void)argv;

    if (argc != 3) {
        write_err_usage("fdisk", " <disk> wipe\n");
        return 1;
    }
    if (!fdisk_load_mbr(disk_index, &block_info, &read_info)) {
        return 1;
    }
    if (!block_info.writable) {
        write_err_str("fdisk: disk is read-only\n");
        return 1;
    }
    for (slot = 0; slot < FDISK_MBR_SLOT_COUNT; slot++) {
        fdisk_zero_slot(fdisk_slot_ptr(read_info.data, slot));
    }
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: wiped partition table on disk");
    write_dec(disk_index);
    write_str("\n");
    return 0;
}

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

static int parse_mount_kind_local(const char *text, uint32_t *kind_out) {
    if (kind_out == NULL) {
        return 0;
    }
    if (streq_local(text, "auto")) {
        *kind_out = NEX_MOUNT_AUTO;
        return 1;
    }
    if (streq_local(text, "fat32")) {
        *kind_out = NEX_MOUNT_FAT32;
        return 1;
    }
    if (streq_local(text, "nxfs")) {
        *kind_out = NEX_MOUNT_NXFS;
        return 1;
    }
    return 0;
}

static const char *cmd_mount_error_message(int rc) {
    switch (-rc) {
        case NEX_MOUNT_ERR_BAD_ARGS:
            return "bad arguments";
        case NEX_MOUNT_ERR_INVALID_SOURCE:
            return "invalid source; use /dev/diskXpY or <disk> <part>";
        case NEX_MOUNT_ERR_INVALID_TARGET:
            return "invalid target path";
        case NEX_MOUNT_ERR_RESERVED_TARGET:
            return "target name is reserved";
        case NEX_MOUNT_ERR_TARGET_EXISTS:
            return "target mount already exists";
        case NEX_MOUNT_ERR_NO_SLOTS:
            return "no free mount slots";
        case NEX_MOUNT_ERR_DISK_NOT_FOUND:
            return "disk not found";
        case NEX_MOUNT_ERR_PARTITION_NOT_FOUND:
            return "partition not found";
        case NEX_MOUNT_ERR_FS_DETECT:
            return "could not detect filesystem";
        case NEX_MOUNT_ERR_UNSUPPORTED_KIND:
            return "unsupported filesystem kind";
        case NEX_MOUNT_ERR_FS_MOUNT:
            return "filesystem mount failed";
        case NEX_MOUNT_ERR_PARTITION_REQUIRED:
            return "filesystem requires a partition, not a raw disk";
        case NEX_MOUNT_ERR_TARGET_BUSY:
            return "target mount is busy";
        case NEX_MOUNT_ERR_TARGET_NOT_FOUND:
            return "target mount not found";
        default:
            return "unknown error";
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
        write_dec(info.start_lba);
        write_str(" sectors=");
        write_dec(info.sector_count);
        write_str(" boot=");
        write_str(info.bootable ? "yes" : "no");
        write_str("\n");
    }
    return 0;
}

int cmd_fdisk(int argc, char **argv) {
    struct syscall_block_info info;
    uint32_t disk_index;
    uint32_t i;

    if (argc == 1) {
        write_str("fdisk disks\n");
        for (i = 0; block_query(i, &info) > 0; i++) {
            fdisk_print_summary_line(&info);
        }
        write_str("usage: fdisk <disk>\n");
        write_str("   or: fdisk <disk> set <slot 1..4> <start_lba> <sectors> <type> [boot]\n");
        write_str("   or: fdisk <disk> clear <slot 1..4>\n");
        write_str("   or: fdisk <disk> boot <slot 1..4>\n");
        write_str("   or: fdisk <disk> wipe\n");
        return 0;
    }
    if (!parse_u32_local(argv[1], &disk_index)) {
        write_err_usage("fdisk", " <disk>\n");
        return 1;
    }
    if (argc == 2) {
        return fdisk_show_disk(disk_index);
    }
    if (streq_ignore_case_local(argv[2], "set")) {
        return fdisk_command_set(disk_index, argc, argv);
    }
    if (streq_ignore_case_local(argv[2], "clear")) {
        return fdisk_command_clear(disk_index, argc, argv);
    }
    if (streq_ignore_case_local(argv[2], "boot")) {
        return fdisk_command_boot(disk_index, argc, argv);
    }
    if (streq_ignore_case_local(argv[2], "wipe")) {
        return fdisk_command_wipe(disk_index, argc, argv);
    }
    write_err_str("fdisk: unknown action\n");
    write_err_str("actions: set clear boot wipe\n");
    return 1;
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

static void stat_print_table_field_local(const char *text);

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

static void df_write_u64_dec_local(uint64_t value) {
    char digits[21];
    uint32_t pos = 0;

    if (value == 0u) {
        write_str("0");
        return;
    }
    while (value != 0u && pos < sizeof(digits)) {
        digits[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos > 0u) {
        pos--;
        write_stdout(&digits[pos], 1);
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
            df_write_u64_dec_local(total);
            write_str(" ");
            df_write_u64_dec_local(used);
            write_str(" ");
            df_write_u64_dec_local(free);
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

enum {
    CPIO_NEWC_HEADER_SIZE = 110u,
    CPIO_MODE_DIR = 0040755u,
    CPIO_MODE_FILE = 0100644u
};

static uint32_t cpio_align4_local(uint32_t value) {
    return (4u - (value & 3u)) & 3u;
}

static void cpio_put_hex8_local(char *dst, uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 7; i >= 0; i--) {
        dst[i] = hex[value & 0x0fu];
        value >>= 4;
    }
}

static int cpio_parse_hex8_local(const char *src, uint32_t *value_out) {
    uint32_t value = 0u;
    uint32_t i;

    if (src == NULL || value_out == NULL) {
        return 0;
    }
    for (i = 0; i < 8u; i++) {
        char ch = src[i];
        uint32_t nibble;

        if (ch >= '0' && ch <= '9') {
            nibble = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = 10u + (uint32_t)(ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = 10u + (uint32_t)(ch - 'A');
        } else {
            return 0;
        }
        value = (value << 4) | nibble;
    }
    *value_out = value;
    return 1;
}

static int cpio_write_all_local(int fd, const void *buf, uint32_t bytes) {
    return (uint32_t)write(fd, buf, bytes) == bytes;
}

static int cpio_read_all_local(int fd, void *buf, uint32_t bytes) {
    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0u;

    while (done < bytes) {
        uint32_t got = (uint32_t)read(fd, dst + done, bytes - done);

        if (got == 0u) {
            return 0;
        }
        done += got;
    }
    return 1;
}

static int cpio_skip_bytes_local(int fd, uint32_t bytes) {
    uint8_t scratch[64];
    uint32_t left = bytes;

    while (left > 0u) {
        uint32_t want = left > sizeof(scratch) ? (uint32_t)sizeof(scratch) : left;

        if (!cpio_read_all_local(fd, scratch, want)) {
            return 0;
        }
        left -= want;
    }
    return 1;
}

static int cpio_write_pad_local(int fd, uint32_t count) {
    static const uint8_t zeros[4] = {0u, 0u, 0u, 0u};

    if (count == 0u) {
        return 1;
    }
    return cpio_write_all_local(fd, zeros, count);
}

static int cpio_is_dot_entry_local(const char *name) {
    return streq_local(name, ".") || streq_local(name, "..");
}

static const char *cpio_path_basename_local(const char *path) {
    const char *last = path;
    uint32_t i = 0u;

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

static void cpio_normalize_name_local(const char *path, char *out, uint32_t out_size) {
    uint32_t src = 0u;
    uint32_t dst = 0u;

    if (out == NULL || out_size == 0u) {
        return;
    }
    while (path != NULL && path[src] == '/') {
        src++;
    }
    if (path == NULL || path[src] == '\0') {
        copy_line_local(out, ".", out_size);
        return;
    }
    while (path[src] != '\0' && dst + 1u < out_size) {
        out[dst++] = path[src++];
    }
    out[dst] = '\0';
}

static int cpio_join_local(char *out, uint32_t out_size, const char *dir, const char *name) {
    if (out == NULL || out_size == 0u || dir == NULL || name == NULL) {
        return 0;
    }
    if (streq_local(dir, ".") || dir[0] == '\0') {
        copy_line_local(out, name, out_size);
        return out[0] != '\0';
    }
    if (streq_local(dir, "/")) {
        if (snprintf(out, out_size, "/%s", name) < 0) {
            return 0;
        }
    } else if (snprintf(out, out_size, "%s/%s", dir, name) < 0) {
        return 0;
    }
    return 1;
}

static int cpio_split_parent_local(const char *path, char *parent, uint32_t parent_size, char *name, uint32_t name_size) {
    uint32_t len;
    uint32_t i;
    uint32_t cut = 0u;

    if (path == NULL || path[0] == '\0' || parent == NULL || name == NULL) {
        return 0;
    }
    len = str_len_local(path);
    while (len > 1u && path[len - 1u] == '/') {
        len--;
    }
    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            cut = i;
        }
    }
    if (cut == 0u) {
        copy_line_local(parent, path[0] == '/' ? "/" : ".", parent_size);
        copy_line_local(name, path[0] == '/' ? path + 1u : path, name_size);
        return name[0] != '\0';
    }
    if (cut >= parent_size || len - cut >= name_size) {
        return 0;
    }
    for (i = 0; i < cut; i++) {
        parent[i] = path[i];
    }
    parent[cut] = '\0';
    copy_line_local(name, path + cut + 1u, name_size);
    return name[0] != '\0';
}

static int cpio_join_absolute_local(char *out, uint32_t out_size, const char *path) {
    char cwd[CMD_PATH_MAX];

    if (out == NULL || out_size == 0u || path == NULL || path[0] == '\0') {
        return 0;
    }
    if (path[0] == '/') {
        copy_line_local(out, path, out_size);
        return out[0] != '\0';
    }
    if (getcwd(cwd, sizeof(cwd)) < 0) {
        copy_line_local(cwd, ".", sizeof(cwd));
    }
    if (streq_local(cwd, "/")) {
        return snprintf(out, out_size, "/%s", path) >= 0;
    }
    return snprintf(out, out_size, "%s/%s", cwd, path) >= 0;
}

static int cpio_path_matches_mount_target_local(const char *path, const char *target) {
    uint32_t target_len;

    if (path == NULL || target == NULL || target[0] == '\0') {
        return 0;
    }
    if (path[0] != '/') {
        return 0;
    }
    target_len = str_len_local(target);
    if (path[1] == '\0') {
        return 0;
    }
    for (uint32_t i = 0; i < target_len; i++) {
        if (path[1u + i] != target[i]) {
            return 0;
        }
    }
    return path[1u + target_len] == '\0' || path[1u + target_len] == '/';
}

static int cpio_path_is_fat_mount_local(const char *path) {
    struct syscall_mount_info info;
    char absolute[CMD_PATH_MAX];

    if (!cpio_join_absolute_local(absolute, sizeof(absolute), path)) {
        return 0;
    }
    for (uint32_t i = 0; mount_query(i, &info) > 0; i++) {
        if (info.kind == NEX_MOUNT_INFO_FAT32 &&
            cpio_path_matches_mount_target_local(absolute, info.target)) {
            return 1;
        }
    }
    return 0;
}

static int cpio_query_path_local(const char *path, uint32_t *attributes_out, uint32_t *size_out) {
    struct syscall_dirent entry;
    char parent[CMD_PATH_MAX];
    char name[CMD_PATH_MAX];
    int ignore_case;
    int fd;

    fd = opendir(path);
    if (fd >= 0) {
        close((uint32_t)fd);
        if (attributes_out != NULL) {
            *attributes_out = 0x10u;
        }
        if (size_out != NULL) {
            *size_out = 0u;
        }
        return 1;
    }
    if (!cpio_split_parent_local(path, parent, sizeof(parent), name, sizeof(name))) {
        return 0;
    }
    fd = opendir(parent);
    if (fd < 0) {
        return 0;
    }
    ignore_case = cpio_path_is_fat_mount_local(parent);
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (streq_local(entry.name, name) ||
            (ignore_case && streq_ignore_case_local(entry.name, name))) {
            close((uint32_t)fd);
            if (attributes_out != NULL) {
                *attributes_out = entry.attributes;
            }
            if (size_out != NULL) {
                *size_out = entry.size;
            }
            return 1;
        }
    }
    close((uint32_t)fd);
    return 0;
}

static const char *stat_type_name_local(uint32_t attributes) {
    return (attributes & 0x10u) != 0u ? "directory" : "file";
}

static void stat_print_table_field_local(const char *text) {
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    write_str("\"");
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            char slash = '\\';
            write_stdout(&slash, 1);
        }
        write_stdout(&text[i], 1);
        i++;
    }
    write_str("\"");
}

enum {
    DU_MAX_DEPTH = 24u
};

struct du_options {
    int all;
    int summary;
    int table;
};

static void du_print_size_local(uint64_t size, const char *path, uint32_t attributes, const struct du_options *opts) {
    if (opts->table) {
        stat_print_table_field_local(path);
        write_str(" ");
        df_write_u64_dec_local(size);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str("\n");
        return;
    }
    write_human_size(size);
    write_str("\t");
    write_str(path);
    write_str("\n");
}

static int du_walk_local(const char *path, const struct du_options *opts, uint32_t depth, uint64_t *size_out) {
    struct syscall_dirent entry;
    uint32_t attributes = 0u;
    uint32_t file_size = 0u;
    uint64_t total = 0u;
    int fd;

    if (path == NULL || opts == NULL || size_out == NULL) {
        return 0;
    }
    if (!cpio_query_path_local(path, &attributes, &file_size)) {
        write_err_str("du: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    if ((attributes & 0x10u) == 0u) {
        total = file_size;
        *size_out = total;
        if (opts->all || depth == 0u) {
            du_print_size_local(total, path, attributes, opts);
        }
        return 1;
    }
    if (depth >= DU_MAX_DEPTH) {
        write_err_str("du: depth limit: ");
        write_err_str(path);
        write_err_str("\n");
        *size_out = 0u;
        return 1;
    }

    fd = opendir(path);
    if (fd < 0) {
        write_err_str("du: cannot open: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char child[CMD_PATH_MAX];
        uint64_t child_size = 0u;

        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        if (!cpio_join_local(child, sizeof(child), path, entry.name)) {
            write_err_str("du: path too long\n");
            continue;
        }
        if (!du_walk_local(child, opts, depth + 1u, &child_size)) {
            close((uint32_t)fd);
            return 0;
        }
        total += child_size;
    }
    close((uint32_t)fd);
    *size_out = total;
    if (!opts->summary || depth == 0u) {
        du_print_size_local(total, path, attributes, opts);
    }
    return 1;
}

int cmd_du(int argc, char **argv) {
    struct du_options opts;
    const char *path = ".";
    int path_set = 0;
    uint64_t total = 0u;

    opts.all = 0;
    opts.summary = 0;
    opts.table = 0;
    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "-a")) {
            opts.all = 1;
        } else if (streq_local(argv[i], "-s")) {
            opts.summary = 1;
        } else if (streq_local(argv[i], "--table")) {
            opts.table = 1;
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_usage("du", " [-a] [-s] [--table] [path]\n");
            return 1;
        }
    }
    if (opts.summary) {
        opts.all = 0;
    }
    if (opts.table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path size type\n");
    }
    return du_walk_local(path, &opts, 0u, &total) ? 0 : 1;
}

static void tree_print_prefix_local(const uint8_t *last_stack, uint32_t depth) {
    uint32_t i;

    for (i = 1u; i < depth; i++) {
        write_str(last_stack[i] ? "   " : "|  ");
    }
}

static int tree_walk_local(const char *path, uint32_t depth, int table, uint8_t *last_stack, int is_last) {
    struct syscall_dirent entry;
    uint32_t entry_count = 0u;
    uint32_t entry_index = 0u;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    int fd;

    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("tree: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    if (table) {
        stat_print_table_field_local(path);
        write_str(" ");
        write_dec(depth);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str(" ");
        write_dec(size);
        write_str("\n");
    } else if (depth == 0u) {
        write_str(path);
        write_str("\n");
    } else {
        tree_print_prefix_local(last_stack, depth);
        write_str(is_last ? "`- " : "|- ");
        write_str(cpio_path_basename_local(path));
        write_str("\n");
    }

    if ((attributes & 0x10u) == 0u) {
        return 1;
    }
    if (depth >= DU_MAX_DEPTH) {
        write_err_str("tree: depth limit: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    fd = opendir(path);
    if (fd < 0) {
        write_err_str("tree: cannot open: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        entry_count++;
    }
    close((uint32_t)fd);

    fd = opendir(path);
    if (fd < 0) {
        write_err_str("tree: cannot reopen: ");
        write_err_str(path);
        write_err_str("\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char child[CMD_PATH_MAX];

        if (cpio_is_dot_entry_local(entry.name)) {
            continue;
        }
        entry_index++;
        if (!cpio_join_local(child, sizeof(child), path, entry.name)) {
            write_err_str("tree: path too long\n");
            continue;
        }
        if (depth + 1u < DU_MAX_DEPTH) {
            last_stack[depth + 1u] = (uint8_t)(entry_index == entry_count);
        }
        if (!tree_walk_local(child, depth + 1u, table, last_stack, entry_index == entry_count)) {
            close((uint32_t)fd);
            return 0;
        }
    }
    close((uint32_t)fd);
    return 1;
}

int cmd_tree(int argc, char **argv) {
    const char *path = ".";
    uint8_t last_stack[DU_MAX_DEPTH + 1u];
    int path_set = 0;
    int table = 0;

    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "--table")) {
            table = 1;
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_usage("tree", " [--table] [path]\n");
            return 1;
        }
    }
    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path depth type size\n");
    }
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(last_stack) / sizeof(last_stack[0])); i++) {
        last_stack[i] = 0u;
    }
    return tree_walk_local(path, 0u, table, last_stack, 1) ? 0 : 1;
}

static int file_has_suffix_local(const char *path, const char *suffix) {
    uint32_t path_len = str_len_local(path);
    uint32_t suffix_len = str_len_local(suffix);

    if (path_len < suffix_len) {
        return 0;
    }
    return streq_ignore_case_local(path + path_len - suffix_len, suffix);
}

static int file_bytes_are_text_local(const uint8_t *bytes, uint32_t count) {
    if (count == 0u) {
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ch = bytes[i];

        if (ch == '\n' || ch == '\r' || ch == '\t') {
            continue;
        }
        if (ch < 32u || ch > 126u) {
            return 0;
        }
    }
    return 1;
}

static const char *file_text_kind_local(const char *path) {
    if (file_has_suffix_local(path, ".cfg")) {
        return "config";
    }
    if (file_has_suffix_local(path, ".asm")) {
        return "assembly";
    }
    if (file_has_suffix_local(path, ".sh")) {
        return "script";
    }
    if (file_has_suffix_local(path, ".txt") || file_has_suffix_local(path, ".md")) {
        return "text";
    }
    return "text";
}

static const char *file_text_detail_local(const char *path) {
    if (file_has_suffix_local(path, ".cfg")) {
        return "ASCII config";
    }
    if (file_has_suffix_local(path, ".asm")) {
        return "assembly source";
    }
    if (file_has_suffix_local(path, ".sh")) {
        return "shell script";
    }
    if (file_has_suffix_local(path, ".md")) {
        return "Markdown text";
    }
    return "ASCII text";
}

static const char *file_detect_kind_local(const char *path,
                                          const uint8_t *bytes,
                                          uint32_t count,
                                          uint32_t size,
                                          const char **detail_out) {
    if (detail_out != NULL) {
        *detail_out = "data";
    }
    if (size == 0u) {
        if (detail_out != NULL) {
            *detail_out = "empty file";
        }
        return "empty";
    }
    if (count >= 4u && bytes[0] == 0x7fu && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
        if (detail_out != NULL) {
            *detail_out = count >= 5u && bytes[4] == 2u ? "ELF64 executable" : "ELF executable";
        }
        return "elf";
    }
    if (count >= 12u &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'A' && bytes[10] == 'V' && bytes[11] == 'E') {
        if (detail_out != NULL) {
            *detail_out = "RIFF WAVE audio";
        }
        return "wav";
    }
    if (count >= 6u &&
        bytes[0] == '0' && bytes[1] == '7' && bytes[2] == '0' &&
        bytes[3] == '7' && bytes[4] == '0' && (bytes[5] == '1' || bytes[5] == '2')) {
        if (detail_out != NULL) {
            *detail_out = "cpio newc archive";
        }
        return "cpio";
    }
    if (file_bytes_are_text_local(bytes, count)) {
        if (detail_out != NULL) {
            *detail_out = file_text_detail_local(path);
        }
        return file_text_kind_local(path);
    }
    if (detail_out != NULL) {
        *detail_out = "binary data";
    }
    return "binary";
}

int cmd_file(int argc, char **argv) {
    const char *path = NULL;
    const char *kind;
    const char *detail;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    uint8_t bytes[128];
    uint32_t got = 0u;
    int table = 0;
    int fd;
    int raw_got;

    if (argc == 3 && streq_local(argv[1], "--table")) {
        table = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        write_err_usage("file", " [--table] <path>\n");
        return 1;
    }
    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("file: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    if ((attributes & 0x10u) != 0u) {
        kind = "directory";
        detail = "directory";
    } else {
        fd = open(path, 0);
        if (fd < 0) {
            write_err_str("file: cannot open: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        raw_got = (int)read(fd, bytes, sizeof(bytes));
        close((uint32_t)fd);
        if (raw_got < 0) {
            write_err_str("file: read failed: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        got = (uint32_t)raw_got;
        kind = file_detect_kind_local(path, bytes, got, size, &detail);
    }
    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path type detail size\n");
        stat_print_table_field_local(path);
        write_str(" ");
        write_str(kind);
        write_str(" ");
        stat_print_table_field_local(detail);
        write_str(" ");
        write_dec(size);
        write_str("\n");
        return 0;
    }
    write_str(path);
    write_str(": ");
    write_str(detail);
    write_str("\n");
    return 0;
}

int cmd_stat(int argc, char **argv) {
    const char *path = NULL;
    uint32_t attributes = 0u;
    uint32_t size = 0u;
    int table = 0;

    if (argc == 3 && streq_local(argv[1], "--table")) {
        table = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        write_err_usage("stat", " [--table] <path>\n");
        return 1;
    }

    if (!cpio_query_path_local(path, &attributes, &size)) {
        write_err_str("stat: not found: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }

    if (table) {
        write_str("# nex/type: table\n");
        write_str("# nex/columns: path type size attr\n");
        stat_print_table_field_local(path);
        write_str(" ");
        write_str(stat_type_name_local(attributes));
        write_str(" ");
        write_dec(size);
        write_str(" 0x");
        write_hex_u32(attributes);
        write_str("\n");
        return 0;
    }

    write_str("path: ");
    write_str(path);
    write_str("\n");
    write_str("type: ");
    write_str(stat_type_name_local(attributes));
    write_str("\n");
    write_str("size: ");
    write_dec(size);
    write_str(" bytes");
    if ((attributes & 0x10u) == 0u) {
        write_str(" (");
        write_human_size(size);
        write_str(")");
    }
    write_str("\n");
    write_str("attr: 0x");
    write_hex_u32(attributes);
    write_str("\n");
    return 0;
}

static int cpio_write_entry_header_local(int fd,
                                         const char *name,
                                         uint32_t mode,
                                         uint32_t file_size,
                                         uint32_t mtime,
                                         uint32_t nlink) {
    char header[CPIO_NEWC_HEADER_SIZE + 1u];
    uint32_t name_size = str_len_local(name) + 1u;

    copy_line_local(header, "070701", sizeof(header));
    cpio_put_hex8_local(header + 6u, 0u);
    cpio_put_hex8_local(header + 14u, mode);
    cpio_put_hex8_local(header + 22u, 0u);
    cpio_put_hex8_local(header + 30u, 0u);
    cpio_put_hex8_local(header + 38u, nlink);
    cpio_put_hex8_local(header + 46u, mtime);
    cpio_put_hex8_local(header + 54u, file_size);
    cpio_put_hex8_local(header + 62u, 0u);
    cpio_put_hex8_local(header + 70u, 0u);
    cpio_put_hex8_local(header + 78u, 0u);
    cpio_put_hex8_local(header + 86u, 0u);
    cpio_put_hex8_local(header + 94u, name_size);
    cpio_put_hex8_local(header + 102u, 0u);
    return cpio_write_all_local(fd, header, CPIO_NEWC_HEADER_SIZE) &&
           cpio_write_all_local(fd, name, name_size) &&
           cpio_write_pad_local(fd, cpio_align4_local(CPIO_NEWC_HEADER_SIZE + name_size));
}

static int cpio_write_file_data_local(int archive_fd, int src_fd, uint32_t size) {
    uint8_t buf[128];
    uint32_t left = size;

    while (left > 0u) {
        uint32_t want = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
        uint32_t got = (uint32_t)read(src_fd, buf, want);

        if (got == 0u || !cpio_write_all_local(archive_fd, buf, got)) {
            return 0;
        }
        left -= got;
    }
    return cpio_write_pad_local(archive_fd, cpio_align4_local(size));
}

static int cpio_archive_path_local(int archive_fd,
                                   const char *disk_path,
                                   const char *archive_name,
                                   uint32_t attributes,
                                   uint32_t size) {
    struct syscall_dirent entry;
    char disk_child[CMD_PATH_MAX];
    char archive_child[CMD_PATH_MAX];
    int fd;

    if ((attributes & 0x10u) != 0u) {
        fd = opendir(disk_path);
        if (fd < 0) {
            return 0;
        }
        if (!cpio_write_entry_header_local(archive_fd, archive_name, CPIO_MODE_DIR, 0u, 0u, 2u)) {
            close((uint32_t)fd);
            return 0;
        }
        while (readdir((uint32_t)fd, &entry) > 0) {
            if (cpio_is_dot_entry_local(entry.name)) {
                continue;
            }
            if (!cpio_join_local(disk_child, sizeof(disk_child), disk_path, entry.name) ||
                !cpio_join_local(archive_child, sizeof(archive_child), archive_name, entry.name) ||
                !cpio_archive_path_local(archive_fd, disk_child, archive_child, entry.attributes, entry.size)) {
                close((uint32_t)fd);
                return 0;
            }
        }
        close((uint32_t)fd);
        return 1;
    }

    fd = open(disk_path, 0);
    if (fd < 0) {
        return 0;
    }
    if (!cpio_write_entry_header_local(archive_fd, archive_name, CPIO_MODE_FILE, size, 0u, 1u) ||
        !cpio_write_file_data_local(archive_fd, fd, size)) {
        close((uint32_t)fd);
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static int cpio_mkdir_parents_local(const char *path) {
    char current[CMD_PATH_MAX];
    uint32_t i = 0u;
    uint32_t len;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    len = str_len_local(path);
    if (len >= sizeof(current)) {
        return 0;
    }
    while (i < len) {
        current[i] = path[i];
        if (path[i] == '/' && i > 0u) {
            current[i] = '\0';
            if (!streq_local(current, ".") && !streq_local(current, "/")) {
                (void)mkdir(current);
            }
            current[i] = '/';
        }
        i++;
    }
    current[len] = '\0';
    return 1;
}

static int cmd_cpio_create_local(int argc, char **argv) {
    char archive_name[CMD_PATH_MAX];
    char member_name[CMD_PATH_MAX];
    uint32_t attributes;
    uint32_t size;
    int fd;
    int i;

    if (argc < 4) {
        write_err_usage("cpio", " -o <archive> <path...>\n");
        return 1;
    }
    fd = open(argv[2], O_CREAT | O_TRUNC);
    if (fd < 0) {
        write_err_str("cpio: archive open failed\n");
        return 1;
    }
    for (i = 3; i < argc; i++) {
        cpio_normalize_name_local(argv[i], archive_name, sizeof(archive_name));
        copy_line_local(member_name, archive_name, sizeof(member_name));
        if (streq_local(member_name, ".")) {
            copy_line_local(member_name, cpio_path_basename_local(argv[i]), sizeof(member_name));
        }
        if (!cpio_query_path_local(argv[i], &attributes, &size) ||
            !cpio_archive_path_local(fd, argv[i], member_name, attributes, size)) {
            close((uint32_t)fd);
            write_err_str("cpio: archive write failed\n");
            return 1;
        }
    }
    if (!cpio_write_entry_header_local(fd, "TRAILER!!!", CPIO_MODE_FILE, 0u, 0u, 1u)) {
        close((uint32_t)fd);
        write_err_str("cpio: trailer write failed\n");
        return 1;
    }
    close((uint32_t)fd);
    return 0;
}

static int cmd_cpio_list_or_extract_local(int argc, char **argv, int extract_mode) {
    char header[CPIO_NEWC_HEADER_SIZE];
    char name[CMD_PATH_MAX];
    char out_path[CMD_PATH_MAX];
    char dest_root[CMD_PATH_MAX];
    uint32_t mode;
    uint32_t file_size;
    uint32_t name_size;
    uint32_t pad;
    int fd;

    if ((!extract_mode && argc != 3) || (extract_mode && argc != 3 && argc != 4)) {
        write_err_usage("cpio", extract_mode ? " -i <archive> [dest]\n" : " -t <archive>\n");
        return 1;
    }
    copy_line_local(dest_root, extract_mode && argc == 4 ? argv[3] : ".", sizeof(dest_root));
    fd = open(argv[2], 0);
    if (fd < 0) {
        write_err_str("cpio: archive open failed\n");
        return 1;
    }

    for (;;) {
        if (!cpio_read_all_local(fd, header, sizeof(header))) {
            close((uint32_t)fd);
            write_err_str("cpio: short header\n");
            return 1;
        }
        if (strncmp(header, "070701", 6u) != 0 ||
            !cpio_parse_hex8_local(header + 14u, &mode) ||
            !cpio_parse_hex8_local(header + 54u, &file_size) ||
            !cpio_parse_hex8_local(header + 94u, &name_size) ||
            name_size == 0u || name_size > sizeof(name)) {
            close((uint32_t)fd);
            write_err_str("cpio: invalid archive\n");
            return 1;
        }
        if (!cpio_read_all_local(fd, name, name_size)) {
            close((uint32_t)fd);
            write_err_str("cpio: short name\n");
            return 1;
        }
        name[name_size - 1u] = '\0';
        pad = cpio_align4_local(CPIO_NEWC_HEADER_SIZE + name_size);
        if (!cpio_skip_bytes_local(fd, pad)) {
            close((uint32_t)fd);
            write_err_str("cpio: short padding\n");
            return 1;
        }
        if (streq_local(name, "TRAILER!!!")) {
            break;
        }

        if (!extract_mode) {
            write_str(((mode >> 12) & 0x0fu) == 4u ? "dir  " : "file ");
            write_str(name);
            write_str(" ");
            write_dec(file_size);
            write_str("\n");
            if (!cpio_skip_bytes_local(fd, file_size + cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short data\n");
                return 1;
            }
            continue;
        }

        if (!cpio_join_local(out_path, sizeof(out_path), dest_root, name) ||
            !cpio_mkdir_parents_local(out_path)) {
            close((uint32_t)fd);
            write_err_str("cpio: path too long\n");
            return 1;
        }
        if (((mode >> 12) & 0x0fu) == 4u) {
            if (mkdir(out_path) != 0 && opendir(out_path) < 0) {
                close((uint32_t)fd);
                write_err_str("cpio: mkdir failed\n");
                return 1;
            }
            if (!cpio_skip_bytes_local(fd, file_size + cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short data\n");
                return 1;
            }
        } else {
            uint8_t buf[128];
            uint32_t left = file_size;
            int out_fd = open(out_path, O_CREAT | O_TRUNC);

            if (out_fd < 0) {
                close((uint32_t)fd);
                write_err_str("cpio: create failed\n");
                return 1;
            }
            while (left > 0u) {
                uint32_t want = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;

                if (!cpio_read_all_local(fd, buf, want) || !cpio_write_all_local(out_fd, buf, want)) {
                    close((uint32_t)out_fd);
                    close((uint32_t)fd);
                    write_err_str("cpio: extract failed\n");
                    return 1;
                }
                left -= want;
            }
            close((uint32_t)out_fd);
            if (!cpio_skip_bytes_local(fd, cpio_align4_local(file_size))) {
                close((uint32_t)fd);
                write_err_str("cpio: short padding\n");
                return 1;
            }
        }
    }
    close((uint32_t)fd);
    return 0;
}

int cmd_cpio(int argc, char **argv) {
    if (argc < 3) {
        write_err_usage("cpio", " -o <archive> <path...> | -t <archive> | -i <archive> [dest]\n");
        return 1;
    }
    if (streq_local(argv[1], "-o")) {
        return cmd_cpio_create_local(argc, argv);
    }
    if (streq_local(argv[1], "-t")) {
        return cmd_cpio_list_or_extract_local(argc, argv, 0);
    }
    if (streq_local(argv[1], "-i")) {
        return cmd_cpio_list_or_extract_local(argc, argv, 1);
    }
    write_err_usage("cpio", " -o <archive> <path...> | -t <archive> | -i <archive> [dest]\n");
    return 1;
}

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
