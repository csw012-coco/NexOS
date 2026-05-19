#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"


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
        case 0xee:
            return "gpt-protective";
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
