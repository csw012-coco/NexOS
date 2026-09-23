#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"


enum {
    FDISK_MBR_BLOCK_SIZE = 512u,
    FDISK_MBR_TABLE_OFFSET = 446u,
    FDISK_MBR_ENTRY_SIZE = 16u,
    FDISK_MBR_SLOT_COUNT = 4u,
    FDISK_DEFAULT_ALIGN = 2048u,
    FDISK_GPT_HEADER_LBA = 1u,
    FDISK_GPT_ENTRY_LBA = 2u,
    FDISK_GPT_ENTRY_SIZE = 128u,
    FDISK_GPT_ENTRY_COUNT = 128u,
    FDISK_GPT_ENTRY_SECTORS = 32u,
    FDISK_GPT_HEADER_SIZE = 92u,
    FDISK_SHELL_LINE_MAX = 160u,
    FDISK_SHELL_ARG_MAX = 12u
};

static const uint8_t fdisk_gpt_guid_zero[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t fdisk_gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

static const uint8_t fdisk_gpt_guid_ms_basic[16] = {
    0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
    0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7
};

static const uint8_t fdisk_gpt_guid_linux_data[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4
};

static uint32_t fdisk_le32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint64_t fdisk_le64(const uint8_t *src) {
    return (uint64_t)fdisk_le32(src) | ((uint64_t)fdisk_le32(src + 4u) << 32);
}

static void fdisk_put_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void fdisk_put_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void fdisk_put_le64(uint8_t *dst, uint64_t value) {
    fdisk_put_le32(dst, (uint32_t)value);
    fdisk_put_le32(dst + 4u, (uint32_t)(value >> 32));
}

static uint8_t *fdisk_slot_ptr(uint8_t *mbr, uint32_t slot) {
    return mbr + FDISK_MBR_TABLE_OFFSET + slot * FDISK_MBR_ENTRY_SIZE;
}

static void fdisk_zero_slot(uint8_t *entry);

static uint32_t fdisk_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xffffffffu;

    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1u) ^ 0xedb88320u;
            } else {
                crc >>= 1u;
            }
        }
    }
    return ~crc;
}

static uint64_t fdisk_align_up_u64(uint64_t value, uint64_t align) {
    if (align == 0u) {
        return value;
    }
    return ((value + align - 1u) / align) * align;
}

static int fdisk_is_100_percent(const char *text) {
    return text != NULL &&
           text[0] == '1' && text[1] == '0' && text[2] == '0' &&
           text[3] == '%' && text[4] == '\0';
}

static void fdisk_make_guid(uint8_t guid[16], uint32_t disk, uint32_t slot, uint64_t a, uint64_t b) {
    uint32_t state = 0x67507421u ^ disk ^ (slot << 16) ^ (uint32_t)a ^
                     (uint32_t)(a >> 32) ^ (uint32_t)b ^ (uint32_t)(b >> 32);

    for (uint32_t i = 0u; i < 16u; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        guid[i] = (uint8_t)(state >> 24);
    }
    guid[6] = (uint8_t)((guid[6] & 0x0fu) | 0x40u);
    guid[8] = (uint8_t)((guid[8] & 0x3fu) | 0x80u);
}

static int fdisk_guid_is_zero(const uint8_t guid[16]) {
    for (uint32_t i = 0u; i < 16u; i++) {
        if (guid[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static void fdisk_copy_guid(uint8_t *dst, const uint8_t *src) {
    for (uint32_t i = 0u; i < 16u; i++) {
        dst[i] = src[i];
    }
}

static const char *fdisk_gpt_type_name(const uint8_t guid[16]) {
    if (memcmp(guid, fdisk_gpt_guid_zero, 16u) == 0) {
        return "empty";
    }
    if (memcmp(guid, fdisk_gpt_guid_efi_system, 16u) == 0) {
        return "efi-system";
    }
    if (memcmp(guid, fdisk_gpt_guid_ms_basic, 16u) == 0) {
        return "ms-basic";
    }
    if (memcmp(guid, fdisk_gpt_guid_linux_data, 16u) == 0) {
        return "linux-data";
    }
    return "unknown";
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

static int fdisk_parse_gpt_slot_local(const char *text, uint32_t *slot_out) {
    uint32_t value;

    if (!parse_u32_local(text, &value) || value == 0u || value > FDISK_GPT_ENTRY_COUNT || slot_out == NULL) {
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

static int fdisk_parse_mbr_type_local(const char *text, uint32_t *type_out) {
    if (text == NULL || type_out == NULL) {
        return 0;
    }
    if (streq_ignore_case_local(text, "fat32") || streq_ignore_case_local(text, "fat")) {
        *type_out = 0x0cu;
        return 1;
    }
    if (streq_ignore_case_local(text, "nxfs") || streq_ignore_case_local(text, "linux")) {
        *type_out = 0x83u;
        return 1;
    }
    if (streq_ignore_case_local(text, "esp") || streq_ignore_case_local(text, "efi")) {
        *type_out = 0xefu;
        return 1;
    }
    return fdisk_parse_type_local(text, type_out);
}

static int fdisk_parse_gpt_type_local(const char *text, const uint8_t **guid_out) {
    if (text == NULL || guid_out == NULL) {
        return 0;
    }
    if (streq_ignore_case_local(text, "esp") || streq_ignore_case_local(text, "efi") ||
        streq_ignore_case_local(text, "fat32") || streq_ignore_case_local(text, "fat")) {
        *guid_out = fdisk_gpt_guid_efi_system;
        return 1;
    }
    if (streq_ignore_case_local(text, "nxfs") || streq_ignore_case_local(text, "linux")) {
        *guid_out = fdisk_gpt_guid_linux_data;
        return 1;
    }
    if (streq_ignore_case_local(text, "basic") || streq_ignore_case_local(text, "ms-basic")) {
        *guid_out = fdisk_gpt_guid_ms_basic;
        return 1;
    }
    return 0;
}

static int fdisk_parse_sector_expr_local(const char *text,
                                         uint64_t disk_blocks,
                                         uint64_t base_lba,
                                         uint64_t *out) {
    uint64_t value = 0u;
    uint64_t scale = 1u;
    uint32_t i = 0u;
    int plus = 0;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    if (text[0] == '+') {
        plus = 1;
        i++;
    }
    if (text[i] == '\0') {
        return 0;
    }
    if (text[i] == '1' && text[i + 1u] == '0' && text[i + 2u] == '0' &&
        text[i + 3u] == '%' && text[i + 4u] == '\0') {
        if (disk_blocks == 0u) {
            *out = 0u;
            return 1;
        }
        *out = plus ? disk_blocks - base_lba : disk_blocks - 1u;
        return disk_blocks > base_lba;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10u + (uint64_t)(text[i] - '0');
        i++;
    }
    if (i == 0u || (plus && i == 1u)) {
        return 0;
    }
    if (text[i] != '\0') {
        char unit = text[i];

        if (text[i + 1u] != '\0') {
            return 0;
        }
        if (unit == 's' || unit == 'S') {
            scale = 1u;
        } else if (unit == 'k' || unit == 'K') {
            scale = 2u;
        } else if (unit == 'm' || unit == 'M') {
            scale = 2048u;
        } else if (unit == 'g' || unit == 'G') {
            scale = 2097152u;
        } else {
            return 0;
        }
    }
    value *= scale;
    *out = plus ? value : value;
    (void)base_lba;
    (void)disk_blocks;
    return value != 0u;
}

static void fdisk_lba_to_chs(uint64_t lba, uint8_t chs[3]) {
    uint64_t cylinder;
    uint32_t head;
    uint32_t sector;

    if (lba > 16450560ull) {
        chs[0] = 0xfeu;
        chs[1] = 0xffu;
        chs[2] = 0xffu;
        return;
    }
    cylinder = lba / (255u * 63u);
    head = (uint32_t)((lba / 63u) % 255u);
    sector = (uint32_t)(lba % 63u) + 1u;
    if (cylinder > 1023u) {
        cylinder = 1023u;
    }
    chs[0] = (uint8_t)head;
    chs[1] = (uint8_t)(sector | ((uint32_t)(cylinder >> 2u) & 0xc0u));
    chs[2] = (uint8_t)(cylinder & 0xffu);
}

static int fdisk_entry_used(const uint8_t *entry) {
    return entry != NULL && entry[4] != 0u && fdisk_le32(entry + 12u) != 0u;
}

static int fdisk_ranges_overlap(uint64_t start_a, uint64_t count_a, uint64_t start_b, uint64_t count_b) {
    uint64_t end_a = start_a + count_a - 1u;
    uint64_t end_b = start_b + count_b - 1u;

    return start_a <= end_b && start_b <= end_a;
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

static int fdisk_read_sector(uint32_t disk_index, uint64_t lba, uint8_t data[FDISK_MBR_BLOCK_SIZE]) {
    struct syscall_block_read_info info;

    if (lba > 0xffffffffull ||
        block_read(disk_index, lba, &info) <= 0 ||
        info.bytes_read != FDISK_MBR_BLOCK_SIZE) {
        return 0;
    }
    for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
        data[i] = info.data[i];
    }
    return 1;
}

static int fdisk_write_sector(uint32_t disk_index, uint64_t lba, const uint8_t data[FDISK_MBR_BLOCK_SIZE]) {
    struct syscall_block_write_info info;

    if (lba > 0xffffffffull) {
        return 0;
    }
    info.disk_index = disk_index;
    info.block_size = FDISK_MBR_BLOCK_SIZE;
    info.bytes_to_write = FDISK_MBR_BLOCK_SIZE;
    info.bytes_written = 0;
    info.lba = lba;
    for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
        info.data[i] = data[i];
    }
    return block_write(disk_index, lba, &info) > 0 && info.bytes_written == FDISK_MBR_BLOCK_SIZE;
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

static int fdisk_mbr_slot_overlaps(const uint8_t *mbr,
                                   uint32_t skip_slot,
                                   uint64_t start_lba,
                                   uint64_t sector_count) {
    for (uint32_t i = 0u; i < FDISK_MBR_SLOT_COUNT; i++) {
        const uint8_t *entry = mbr + FDISK_MBR_TABLE_OFFSET + i * FDISK_MBR_ENTRY_SIZE;
        uint64_t other_start;
        uint64_t other_count;

        if (i == skip_slot || !fdisk_entry_used(entry)) {
            continue;
        }
        other_start = fdisk_le32(entry + 8u);
        other_count = fdisk_le32(entry + 12u);
        if (fdisk_ranges_overlap(start_lba, sector_count, other_start, other_count)) {
            return 1;
        }
    }
    return 0;
}

static uint64_t fdisk_mbr_find_free_start(const uint8_t *mbr,
                                          uint64_t disk_blocks,
                                          uint64_t sector_count) {
    uint64_t start = FDISK_DEFAULT_ALIGN;

    for (;;) {
        uint64_t next = start;
        int moved = 0;

        if (sector_count == 0u || start >= disk_blocks || sector_count > disk_blocks - start) {
            return 0u;
        }
        for (uint32_t i = 0u; i < FDISK_MBR_SLOT_COUNT; i++) {
            const uint8_t *entry = mbr + FDISK_MBR_TABLE_OFFSET + i * FDISK_MBR_ENTRY_SIZE;
            uint64_t other_start;
            uint64_t other_count;

            if (!fdisk_entry_used(entry)) {
                continue;
            }
            other_start = fdisk_le32(entry + 8u);
            other_count = fdisk_le32(entry + 12u);
            if (fdisk_ranges_overlap(start, sector_count, other_start, other_count)) {
                next = fdisk_align_up_u64(other_start + other_count, FDISK_DEFAULT_ALIGN);
                moved = 1;
            }
        }
        if (!moved) {
            return start;
        }
        start = next;
    }
}

static uint64_t fdisk_mbr_next_partition_start(const uint8_t *mbr,
                                               uint32_t skip_slot,
                                               uint64_t start_lba,
                                               uint64_t disk_blocks) {
    uint64_t next = disk_blocks;

    for (uint32_t i = 0u; i < FDISK_MBR_SLOT_COUNT; i++) {
        const uint8_t *entry = mbr + FDISK_MBR_TABLE_OFFSET + i * FDISK_MBR_ENTRY_SIZE;
        uint64_t other_start;

        if (i == skip_slot || !fdisk_entry_used(entry)) {
            continue;
        }
        other_start = fdisk_le32(entry + 8u);
        if (other_start > start_lba && other_start < next) {
            next = other_start;
        }
    }
    return next;
}

static void fdisk_write_mbr_entry(uint8_t *entry,
                                  uint64_t start_lba,
                                  uint64_t sector_count,
                                  uint32_t type,
                                  int bootable) {
    uint8_t start_chs[3];
    uint8_t end_chs[3];

    fdisk_zero_slot(entry);
    fdisk_lba_to_chs(start_lba, start_chs);
    fdisk_lba_to_chs(start_lba + sector_count - 1u, end_chs);
    entry[0] = bootable ? 0x80u : 0x00u;
    entry[1] = start_chs[0];
    entry[2] = start_chs[1];
    entry[3] = start_chs[2];
    entry[4] = (uint8_t)type;
    entry[5] = end_chs[0];
    entry[6] = end_chs[1];
    entry[7] = end_chs[2];
    fdisk_put_le32(entry + 8, (uint32_t)start_lba);
    fdisk_put_le32(entry + 12, (uint32_t)sector_count);
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
    uint64_t start_lba;
    uint64_t sector_count;
    uint32_t type;
    uint8_t *entry;
    uint32_t i;
    int bootable = 0;

    if (argc != 7 && argc != 8) {
        write_err_usage("fdisk", " <disk> set <slot 1..4> <start_lba> <sectors> <type> [boot]\n");
        return 1;
    }
    if (!fdisk_parse_slot_local(argv[3], &slot) ||
        !fdisk_parse_sector_expr_local(argv[4], 0u, 0u, &start_lba) ||
        !fdisk_parse_sector_expr_local(argv[5], 0u, 0u, &sector_count) ||
        !fdisk_parse_mbr_type_local(argv[6], &type)) {
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
    if (start_lba == 0u || start_lba >= block_info.block_count ||
        start_lba > 0xffffffffull ||
        sector_count > 0xffffffffull ||
        sector_count > block_info.block_count - start_lba) {
        write_err_str("fdisk: partition is outside disk bounds\n");
        return 1;
    }
    if (fdisk_mbr_slot_overlaps(read_info.data, slot, start_lba, sector_count)) {
        write_err_str("fdisk: partition overlaps existing slot\n");
        return 1;
    }
    entry = fdisk_slot_ptr(read_info.data, slot);
    fdisk_write_mbr_entry(entry, start_lba, sector_count, type, bootable);
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

static int fdisk_command_add(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint64_t sector_count;
    uint64_t start_lba;
    uint32_t type;
    uint32_t slot = FDISK_MBR_SLOT_COUNT;
    int bootable = 0;

    if (argc != 5 && argc != 6) {
        write_err_usage("fdisk", " <disk> add <sectors|+size|100%> <type> [boot]\n");
        return 1;
    }
    if (!fdisk_parse_sector_expr_local(argv[3], 0u, 0u, &sector_count) ||
        !fdisk_parse_mbr_type_local(argv[4], &type)) {
        write_err_str("fdisk: invalid add argument\n");
        return 1;
    }
    if (argc == 6) {
        if (!streq_ignore_case_local(argv[5], "boot") && !streq_ignore_case_local(argv[5], "active")) {
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
    for (uint32_t i = 0u; i < FDISK_MBR_SLOT_COUNT; i++) {
        if (!fdisk_entry_used(fdisk_slot_ptr(read_info.data, i))) {
            slot = i;
            break;
        }
    }
    if (slot >= FDISK_MBR_SLOT_COUNT) {
        write_err_str("fdisk: no empty MBR slot\n");
        return 1;
    }
    if (fdisk_is_100_percent(argv[3])) {
        start_lba = fdisk_mbr_find_free_start(read_info.data, block_info.block_count, 1u);
        if (start_lba != 0u) {
            sector_count = fdisk_mbr_next_partition_start(read_info.data, slot, start_lba,
                                                          block_info.block_count) - start_lba;
        }
    } else {
        start_lba = fdisk_mbr_find_free_start(read_info.data, block_info.block_count, sector_count);
    }
    if (start_lba == 0u || sector_count > 0xffffffffull ||
        start_lba > 0xffffffffull || sector_count > block_info.block_count - start_lba) {
        write_err_str("fdisk: no suitable free range\n");
        return 1;
    }
    fdisk_write_mbr_entry(fdisk_slot_ptr(read_info.data, slot), start_lba, sector_count, type, bootable);
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: added slot ");
    write_dec(slot + 1u);
    write_str(" lba=");
    write_dec((uint32_t)start_lba);
    write_str(" sectors=");
    write_dec((uint32_t)sector_count);
    write_str("\n");
    return 0;
}

static int fdisk_command_resize(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    struct syscall_block_read_info read_info;
    uint32_t slot;
    uint8_t *entry;
    uint64_t start_lba;
    uint64_t sector_count;
    uint32_t type;
    int bootable;

    if (argc != 5) {
        write_err_usage("fdisk", " <disk> resize <slot 1..4> <sectors|+size|100%>\n");
        return 1;
    }
    if (!fdisk_parse_slot_local(argv[3], &slot) ||
        !fdisk_parse_sector_expr_local(argv[4], 0u, 0u, &sector_count)) {
        write_err_str("fdisk: invalid resize argument\n");
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
    if (!fdisk_entry_used(entry)) {
        write_err_str("fdisk: selected slot is empty\n");
        return 1;
    }
    start_lba = fdisk_le32(entry + 8u);
    if (fdisk_is_100_percent(argv[4])) {
        sector_count = fdisk_mbr_next_partition_start(read_info.data, slot, start_lba,
                                                      block_info.block_count) - start_lba;
    }
    type = entry[4];
    bootable = (entry[0] & 0x80u) != 0u;
    if (sector_count == 0u || sector_count > 0xffffffffull ||
        start_lba >= block_info.block_count || sector_count > block_info.block_count - start_lba) {
        write_err_str("fdisk: resized partition is outside disk bounds\n");
        return 1;
    }
    if (fdisk_mbr_slot_overlaps(read_info.data, slot, start_lba, sector_count)) {
        write_err_str("fdisk: resized partition overlaps existing slot\n");
        return 1;
    }
    fdisk_write_mbr_entry(entry, start_lba, sector_count, type, bootable);
    fdisk_update_signature(read_info.data);
    if (!fdisk_store_mbr(disk_index, read_info.data)) {
        write_err_str("fdisk: mbr write failed\n");
        return 1;
    }
    write_str("fdisk: resized slot ");
    write_dec(slot + 1u);
    write_str(" sectors=");
    write_dec((uint32_t)sector_count);
    write_str("\n");
    return 0;
}

static void fdisk_gpt_set_header(uint8_t header[FDISK_MBR_BLOCK_SIZE],
                                 uint64_t current_lba,
                                 uint64_t backup_lba,
                                 uint64_t first_usable,
                                 uint64_t last_usable,
                                 uint64_t entries_lba,
                                 const uint8_t disk_guid[16],
                                 uint32_t entries_crc) {
    for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
        header[i] = 0u;
    }
    header[0] = 'E';
    header[1] = 'F';
    header[2] = 'I';
    header[3] = ' ';
    header[4] = 'P';
    header[5] = 'A';
    header[6] = 'R';
    header[7] = 'T';
    fdisk_put_le32(header + 8u, 0x00010000u);
    fdisk_put_le32(header + 12u, FDISK_GPT_HEADER_SIZE);
    fdisk_put_le64(header + 24u, current_lba);
    fdisk_put_le64(header + 32u, backup_lba);
    fdisk_put_le64(header + 40u, first_usable);
    fdisk_put_le64(header + 48u, last_usable);
    fdisk_copy_guid(header + 56u, disk_guid);
    fdisk_put_le64(header + 72u, entries_lba);
    fdisk_put_le32(header + 80u, FDISK_GPT_ENTRY_COUNT);
    fdisk_put_le32(header + 84u, FDISK_GPT_ENTRY_SIZE);
    fdisk_put_le32(header + 88u, entries_crc);
    fdisk_put_le32(header + 16u, fdisk_crc32(header, FDISK_GPT_HEADER_SIZE));
}

static int fdisk_gpt_load_header(uint32_t disk_index,
                                 uint8_t header[FDISK_MBR_BLOCK_SIZE],
                                 uint64_t *first_usable_out,
                                 uint64_t *last_usable_out,
                                 uint64_t *entries_lba_out,
                                 uint32_t *entry_count_out,
                                 uint32_t *entry_size_out) {
    if (!fdisk_read_sector(disk_index, FDISK_GPT_HEADER_LBA, header)) {
        write_err_str("fdisk: GPT header read failed\n");
        return 0;
    }
    if (memcmp(header, "EFI PART", 8u) != 0 ||
        fdisk_le32(header + 12u) < FDISK_GPT_HEADER_SIZE ||
        fdisk_le32(header + 84u) < FDISK_GPT_ENTRY_SIZE) {
        write_err_str("fdisk: no usable GPT; run 'fdisk <disk> gpt init'\n");
        return 0;
    }
    if (first_usable_out != NULL) {
        *first_usable_out = fdisk_le64(header + 40u);
    }
    if (last_usable_out != NULL) {
        *last_usable_out = fdisk_le64(header + 48u);
    }
    if (entries_lba_out != NULL) {
        *entries_lba_out = fdisk_le64(header + 72u);
    }
    if (entry_count_out != NULL) {
        *entry_count_out = fdisk_le32(header + 80u);
    }
    if (entry_size_out != NULL) {
        *entry_size_out = fdisk_le32(header + 84u);
    }
    return 1;
}

static int fdisk_gpt_read_entry(uint32_t disk_index,
                                uint64_t entries_lba,
                                uint32_t slot,
                                uint8_t entry[FDISK_GPT_ENTRY_SIZE]) {
    uint8_t block[FDISK_MBR_BLOCK_SIZE];
    uint32_t offset = (slot * FDISK_GPT_ENTRY_SIZE) % FDISK_MBR_BLOCK_SIZE;
    uint64_t lba = entries_lba + ((slot * FDISK_GPT_ENTRY_SIZE) / FDISK_MBR_BLOCK_SIZE);

    if (!fdisk_read_sector(disk_index, lba, block)) {
        return 0;
    }
    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_SIZE; i++) {
        entry[i] = block[offset + i];
    }
    return 1;
}

static int fdisk_gpt_write_entry(uint32_t disk_index,
                                 uint64_t entries_lba,
                                 uint32_t slot,
                                 const uint8_t entry[FDISK_GPT_ENTRY_SIZE]) {
    uint8_t block[FDISK_MBR_BLOCK_SIZE];
    uint32_t offset = (slot * FDISK_GPT_ENTRY_SIZE) % FDISK_MBR_BLOCK_SIZE;
    uint64_t lba = entries_lba + ((slot * FDISK_GPT_ENTRY_SIZE) / FDISK_MBR_BLOCK_SIZE);

    if (!fdisk_read_sector(disk_index, lba, block)) {
        return 0;
    }
    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_SIZE; i++) {
        block[offset + i] = entry[i];
    }
    return fdisk_write_sector(disk_index, lba, block);
}

static uint32_t fdisk_gpt_entries_crc(uint32_t disk_index, uint64_t entries_lba, uint32_t entry_count) {
    uint8_t block[FDISK_MBR_BLOCK_SIZE];
    uint8_t bytes[FDISK_GPT_ENTRY_SECTORS * FDISK_MBR_BLOCK_SIZE];
    uint32_t sectors = (entry_count * FDISK_GPT_ENTRY_SIZE + FDISK_MBR_BLOCK_SIZE - 1u) /
                       FDISK_MBR_BLOCK_SIZE;
    uint32_t pos = 0u;

    if (sectors > FDISK_GPT_ENTRY_SECTORS) {
        sectors = FDISK_GPT_ENTRY_SECTORS;
    }
    for (uint32_t s = 0u; s < sectors; s++) {
        if (!fdisk_read_sector(disk_index, entries_lba + s, block)) {
            return 0u;
        }
        for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
            bytes[pos++] = block[i];
        }
    }
    return fdisk_crc32(bytes, entry_count * FDISK_GPT_ENTRY_SIZE);
}

static int fdisk_gpt_copy_entries(uint32_t disk_index, uint64_t src_lba, uint64_t dst_lba) {
    uint8_t block[FDISK_MBR_BLOCK_SIZE];

    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_SECTORS; i++) {
        if (!fdisk_read_sector(disk_index, src_lba + i, block) ||
            !fdisk_write_sector(disk_index, dst_lba + i, block)) {
            return 0;
        }
    }
    return 1;
}

static int fdisk_gpt_update_headers(uint32_t disk_index, uint64_t disk_blocks) {
    uint8_t primary[FDISK_MBR_BLOCK_SIZE];
    uint8_t backup[FDISK_MBR_BLOCK_SIZE];
    uint8_t mbr[FDISK_MBR_BLOCK_SIZE];
    uint8_t guid[16];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t primary_entries_lba;
    uint64_t backup_entries_lba = disk_blocks - 1u - FDISK_GPT_ENTRY_SECTORS;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc;

    if (!fdisk_gpt_load_header(disk_index, primary, &first_usable, &last_usable,
                               &primary_entries_lba, &entry_count, &entry_size)) {
        return 0;
    }
    if (entry_count > FDISK_GPT_ENTRY_COUNT || entry_size != FDISK_GPT_ENTRY_SIZE) {
        write_err_str("fdisk: unsupported GPT entry table\n");
        return 0;
    }
    fdisk_copy_guid(guid, primary + 56u);
    if (!fdisk_gpt_copy_entries(disk_index, primary_entries_lba, backup_entries_lba)) {
        write_err_str("fdisk: GPT backup entries write failed\n");
        return 0;
    }
    entries_crc = fdisk_gpt_entries_crc(disk_index, primary_entries_lba, entry_count);
    fdisk_gpt_set_header(primary, FDISK_GPT_HEADER_LBA, disk_blocks - 1u,
                         first_usable, last_usable, primary_entries_lba, guid, entries_crc);
    fdisk_gpt_set_header(backup, disk_blocks - 1u, FDISK_GPT_HEADER_LBA,
                         first_usable, last_usable, backup_entries_lba, guid, entries_crc);
    if (!fdisk_write_sector(disk_index, FDISK_GPT_HEADER_LBA, primary) ||
        !fdisk_write_sector(disk_index, disk_blocks - 1u, backup)) {
        return 0;
    }
    if (fdisk_read_sector(disk_index, 0u, mbr)) {
        (void)fdisk_store_mbr(disk_index, mbr);
    }
    return 1;
}

static void fdisk_gpt_write_name(uint8_t entry[FDISK_GPT_ENTRY_SIZE], const char *name) {
    uint32_t i = 0u;

    if (name == NULL || name[0] == '\0') {
        name = "NexOS";
    }
    while (name[i] != '\0' && i < 36u) {
        fdisk_put_le16(entry + 56u + i * 2u, (uint16_t)(uint8_t)name[i]);
        i++;
    }
}

static void fdisk_gpt_print_entry(uint32_t slot, const uint8_t entry[FDISK_GPT_ENTRY_SIZE]) {
    uint64_t first;
    uint64_t last;

    if (fdisk_guid_is_zero(entry)) {
        return;
    }
    first = fdisk_le64(entry + 32u);
    last = fdisk_le64(entry + 40u);
    write_str("gpt ");
    write_dec(slot + 1u);
    write_str(": type=");
    write_str(fdisk_gpt_type_name(entry));
    write_str(" lba=");
    storage_write_u64_dec(first);
    write_str(" sectors=");
    storage_write_u64_dec(last - first + 1u);
    write_str(" end=");
    storage_write_u64_dec(last);
    write_str("\n");
}

static int fdisk_gpt_entry_used(const uint8_t entry[FDISK_GPT_ENTRY_SIZE]) {
    return !fdisk_guid_is_zero(entry) && fdisk_le64(entry + 40u) >= fdisk_le64(entry + 32u);
}

static int fdisk_gpt_range_overlaps(uint32_t disk_index,
                                    uint64_t entries_lba,
                                    uint32_t skip_slot,
                                    uint64_t first_lba,
                                    uint64_t sector_count) {
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];

    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_COUNT; i++) {
        uint64_t other_first;
        uint64_t other_count;

        if (i == skip_slot || !fdisk_gpt_read_entry(disk_index, entries_lba, i, entry) ||
            !fdisk_gpt_entry_used(entry)) {
            continue;
        }
        other_first = fdisk_le64(entry + 32u);
        other_count = fdisk_le64(entry + 40u) - other_first + 1u;
        if (fdisk_ranges_overlap(first_lba, sector_count, other_first, other_count)) {
            return 1;
        }
    }
    return 0;
}

static uint64_t fdisk_gpt_find_free_start(uint32_t disk_index,
                                          uint64_t entries_lba,
                                          uint64_t first_usable,
                                          uint64_t last_usable,
                                          uint64_t sector_count) {
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];
    uint64_t start = fdisk_align_up_u64(first_usable, FDISK_DEFAULT_ALIGN);

    for (;;) {
        uint64_t next = start;
        int moved = 0;

        if (sector_count == 0u || start > last_usable || sector_count - 1u > last_usable - start) {
            return 0u;
        }
        for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_COUNT; i++) {
            uint64_t other_first;
            uint64_t other_count;

            if (!fdisk_gpt_read_entry(disk_index, entries_lba, i, entry) ||
                !fdisk_gpt_entry_used(entry)) {
                continue;
            }
            other_first = fdisk_le64(entry + 32u);
            other_count = fdisk_le64(entry + 40u) - other_first + 1u;
            if (fdisk_ranges_overlap(start, sector_count, other_first, other_count)) {
                next = fdisk_align_up_u64(other_first + other_count, FDISK_DEFAULT_ALIGN);
                moved = 1;
            }
        }
        if (!moved) {
            return start;
        }
        start = next;
    }
}

static int fdisk_gpt_first_empty_slot(uint32_t disk_index, uint64_t entries_lba, uint32_t *slot_out) {
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];

    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_COUNT; i++) {
        if (!fdisk_gpt_read_entry(disk_index, entries_lba, i, entry)) {
            return 0;
        }
        if (!fdisk_gpt_entry_used(entry)) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

static int fdisk_command_gpt_show(uint32_t disk_index) {
    struct syscall_block_info block_info;
    uint8_t header[FDISK_MBR_BLOCK_SIZE];
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;

    if (block_query(disk_index, &block_info) <= 0) {
        write_err_str("fdisk: disk not found\n");
        return 1;
    }
    if (!fdisk_gpt_load_header(disk_index, header, &first_usable, &last_usable,
                               &entries_lba, &entry_count, &entry_size)) {
        return 1;
    }
    write_str("gpt disk");
    write_dec(disk_index);
    write_str(": first=");
    storage_write_u64_dec(first_usable);
    write_str(" last=");
    storage_write_u64_dec(last_usable);
    write_str(" entries=");
    write_dec(entry_count);
    write_str("\n");
    for (uint32_t i = 0u; i < entry_count && i < FDISK_GPT_ENTRY_COUNT; i++) {
        if (fdisk_gpt_read_entry(disk_index, entries_lba, i, entry)) {
            fdisk_gpt_print_entry(i, entry);
        }
    }
    return 0;
}

static int fdisk_command_gpt_init(uint32_t disk_index) {
    struct syscall_block_info block_info;
    uint8_t block[FDISK_MBR_BLOCK_SIZE];
    uint8_t disk_guid[16];
    uint64_t disk_blocks;
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t backup_entries_lba;
    uint32_t entries_crc;

    if (block_query(disk_index, &block_info) <= 0) {
        write_err_str("fdisk: disk not found\n");
        return 1;
    }
    if (!block_info.writable || block_info.block_size != FDISK_MBR_BLOCK_SIZE) {
        write_err_str("fdisk: disk is not writable 512-byte media\n");
        return 1;
    }
    disk_blocks = block_info.block_count;
    if (disk_blocks < 68u || disk_blocks > 0xffffffffull) {
        write_err_str("fdisk: disk is too small or too large for this GPT editor\n");
        return 1;
    }
    first_usable = FDISK_GPT_ENTRY_LBA + FDISK_GPT_ENTRY_SECTORS;
    backup_entries_lba = disk_blocks - 1u - FDISK_GPT_ENTRY_SECTORS;
    last_usable = backup_entries_lba - 1u;
    fdisk_make_guid(disk_guid, disk_index, 0u, disk_blocks, first_usable);

    for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
        block[i] = 0u;
    }
    fdisk_write_mbr_entry(fdisk_slot_ptr(block, 0u), 1u,
                          disk_blocks - 1u > 0xffffffffull ? 0xffffffffull : disk_blocks - 1u,
                          0xeeu, 0);
    fdisk_update_signature(block);
    if (!fdisk_store_mbr(disk_index, block)) {
        write_err_str("fdisk: protective MBR write failed\n");
        return 1;
    }
    for (uint32_t s = 0u; s < FDISK_GPT_ENTRY_SECTORS; s++) {
        for (uint32_t i = 0u; i < FDISK_MBR_BLOCK_SIZE; i++) {
            block[i] = 0u;
        }
        if (!fdisk_write_sector(disk_index, FDISK_GPT_ENTRY_LBA + s, block) ||
            !fdisk_write_sector(disk_index, backup_entries_lba + s, block)) {
            write_err_str("fdisk: GPT entries clear failed\n");
            return 1;
        }
    }
    entries_crc = fdisk_gpt_entries_crc(disk_index, FDISK_GPT_ENTRY_LBA, FDISK_GPT_ENTRY_COUNT);
    fdisk_gpt_set_header(block, FDISK_GPT_HEADER_LBA, disk_blocks - 1u,
                         first_usable, last_usable, FDISK_GPT_ENTRY_LBA, disk_guid, entries_crc);
    if (!fdisk_write_sector(disk_index, FDISK_GPT_HEADER_LBA, block)) {
        write_err_str("fdisk: GPT header write failed\n");
        return 1;
    }
    fdisk_gpt_set_header(block, disk_blocks - 1u, FDISK_GPT_HEADER_LBA,
                         first_usable, last_usable, backup_entries_lba, disk_guid, entries_crc);
    if (!fdisk_write_sector(disk_index, disk_blocks - 1u, block)) {
        write_err_str("fdisk: GPT backup header write failed\n");
        return 1;
    }
    write_str("fdisk: initialized GPT on disk");
    write_dec(disk_index);
    write_str("\n");
    return 0;
}

static int fdisk_command_gpt_add(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    uint8_t header[FDISK_MBR_BLOCK_SIZE];
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t entries_lba;
    uint64_t sector_count;
    uint64_t start_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t slot;
    const uint8_t *type_guid;
    const char *name = "NexOS";

    if (argc < 6 || argc > 7) {
        write_err_usage("fdisk", " <disk> gpt add <sectors|+size|100%> <type> [name]\n");
        return 1;
    }
    if (!fdisk_parse_sector_expr_local(argv[4], 0u, 0u, &sector_count) ||
        !fdisk_parse_gpt_type_local(argv[5], &type_guid)) {
        write_err_str("fdisk: invalid GPT add argument\n");
        return 1;
    }
    if (argc == 7) {
        name = argv[6];
    }
    if (block_query(disk_index, &block_info) <= 0 || !block_info.writable ||
        !fdisk_gpt_load_header(disk_index, header, &first_usable, &last_usable,
                               &entries_lba, &entry_count, &entry_size)) {
        return 1;
    }
    if (fdisk_is_100_percent(argv[4])) {
        sector_count = last_usable - first_usable + 1u;
    }
    if (!fdisk_gpt_first_empty_slot(disk_index, entries_lba, &slot)) {
        write_err_str("fdisk: no empty GPT slot\n");
        return 1;
    }
    start_lba = fdisk_gpt_find_free_start(disk_index, entries_lba, first_usable, last_usable, sector_count);
    if (start_lba == 0u) {
        write_err_str("fdisk: no suitable free GPT range\n");
        return 1;
    }
    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_SIZE; i++) {
        entry[i] = 0u;
    }
    fdisk_copy_guid(entry, type_guid);
    fdisk_make_guid(entry + 16u, disk_index, slot, start_lba, sector_count);
    fdisk_put_le64(entry + 32u, start_lba);
    fdisk_put_le64(entry + 40u, start_lba + sector_count - 1u);
    fdisk_gpt_write_name(entry, name);
    if (!fdisk_gpt_write_entry(disk_index, entries_lba, slot, entry) ||
        !fdisk_gpt_update_headers(disk_index, block_info.block_count)) {
        write_err_str("fdisk: GPT add write failed\n");
        return 1;
    }
    write_str("fdisk: added GPT ");
    write_dec(slot + 1u);
    write_str(" lba=");
    storage_write_u64_dec(start_lba);
    write_str(" sectors=");
    storage_write_u64_dec(sector_count);
    write_str("\n");
    return 0;
}

static int fdisk_command_gpt_clear(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    uint8_t header[FDISK_MBR_BLOCK_SIZE];
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t slot;

    if (argc != 5 || !fdisk_parse_gpt_slot_local(argv[4], &slot)) {
        write_err_usage("fdisk", " <disk> gpt clear <slot 1..128>\n");
        return 1;
    }
    if (block_query(disk_index, &block_info) <= 0 || !block_info.writable ||
        !fdisk_gpt_load_header(disk_index, header, &first_usable, &last_usable,
                               &entries_lba, &entry_count, &entry_size)) {
        return 1;
    }
    for (uint32_t i = 0u; i < FDISK_GPT_ENTRY_SIZE; i++) {
        entry[i] = 0u;
    }
    if (!fdisk_gpt_write_entry(disk_index, entries_lba, slot, entry) ||
        !fdisk_gpt_update_headers(disk_index, block_info.block_count)) {
        write_err_str("fdisk: GPT clear write failed\n");
        return 1;
    }
    write_str("fdisk: cleared GPT ");
    write_dec(slot + 1u);
    write_str("\n");
    return 0;
}

static int fdisk_command_gpt_resize(uint32_t disk_index, int argc, char **argv) {
    struct syscall_block_info block_info;
    uint8_t header[FDISK_MBR_BLOCK_SIZE];
    uint8_t entry[FDISK_GPT_ENTRY_SIZE];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t entries_lba;
    uint64_t sector_count;
    uint64_t start_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t slot;

    if (argc != 6 || !fdisk_parse_gpt_slot_local(argv[4], &slot) ||
        !fdisk_parse_sector_expr_local(argv[5], 0u, 0u, &sector_count)) {
        write_err_usage("fdisk", " <disk> gpt resize <slot 1..128> <sectors|+size|100%>\n");
        return 1;
    }
    if (block_query(disk_index, &block_info) <= 0 || !block_info.writable ||
        !fdisk_gpt_load_header(disk_index, header, &first_usable, &last_usable,
                               &entries_lba, &entry_count, &entry_size)) {
        return 1;
    }
    if (!fdisk_gpt_read_entry(disk_index, entries_lba, slot, entry) ||
        !fdisk_gpt_entry_used(entry)) {
        write_err_str("fdisk: selected GPT slot is empty\n");
        return 1;
    }
    start_lba = fdisk_le64(entry + 32u);
    if (fdisk_is_100_percent(argv[5])) {
        sector_count = last_usable - start_lba + 1u;
    }
    if (sector_count == 0u || start_lba < first_usable || start_lba > last_usable ||
        sector_count - 1u > last_usable - start_lba ||
        fdisk_gpt_range_overlaps(disk_index, entries_lba, slot, start_lba, sector_count)) {
        write_err_str("fdisk: invalid or overlapping GPT resize\n");
        return 1;
    }
    fdisk_put_le64(entry + 40u, start_lba + sector_count - 1u);
    if (!fdisk_gpt_write_entry(disk_index, entries_lba, slot, entry) ||
        !fdisk_gpt_update_headers(disk_index, block_info.block_count)) {
        write_err_str("fdisk: GPT resize write failed\n");
        return 1;
    }
    write_str("fdisk: resized GPT ");
    write_dec(slot + 1u);
    write_str(" sectors=");
    storage_write_u64_dec(sector_count);
    write_str("\n");
    return 0;
}

static int fdisk_command_gpt(uint32_t disk_index, int argc, char **argv) {
    if (argc == 3 || (argc == 4 && streq_ignore_case_local(argv[3], "show"))) {
        return fdisk_command_gpt_show(disk_index);
    }
    if (argc >= 4 && streq_ignore_case_local(argv[3], "init")) {
        return fdisk_command_gpt_init(disk_index);
    }
    if (argc >= 4 && streq_ignore_case_local(argv[3], "add")) {
        return fdisk_command_gpt_add(disk_index, argc, argv);
    }
    if (argc >= 4 && streq_ignore_case_local(argv[3], "clear")) {
        return fdisk_command_gpt_clear(disk_index, argc, argv);
    }
    if (argc >= 4 && streq_ignore_case_local(argv[3], "resize")) {
        return fdisk_command_gpt_resize(disk_index, argc, argv);
    }
    write_err_usage("fdisk", " <disk> gpt [show|init|add|clear|resize]\n");
    return 1;
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

static void fdisk_u32_to_text(uint32_t value, char out[12]) {
    char tmp[12];
    uint32_t pos = 0u;
    uint32_t out_pos = 0u;

    if (value == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value != 0u && pos < sizeof(tmp)) {
        tmp[pos++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (pos != 0u) {
        out[out_pos++] = tmp[--pos];
    }
    out[out_pos] = '\0';
}

static int fdisk_shell_read_line(char *line, uint32_t size) {
    uint32_t pos = 0u;
    char ch;

    if (line == NULL || size == 0u) {
        return 0;
    }
    while (pos + 1u < size) {
        ssize_t got = read(0, &ch, 1u);

        if (got <= 0) {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        line[pos++] = ch;
    }
    line[pos] = '\0';
    return pos != 0u;
}

static uint32_t fdisk_shell_split(char *line, char **argv, uint32_t max_args) {
    uint32_t argc = 0u;
    uint32_t i = 0u;

    while (line[i] != '\0' && argc < max_args) {
        while (line[i] == ' ' || line[i] == '\t') {
            line[i++] = '\0';
        }
        if (line[i] == '\0') {
            break;
        }
        argv[argc++] = line + i;
        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
    }
    return argc;
}

static int fdisk_shell_execute(uint32_t disk_index, char **args, uint32_t arg_count) {
    char disk_text[12];
    char *fake[FDISK_SHELL_ARG_MAX + 3u];

    if (arg_count == 0u) {
        return 0;
    }
    if (streq_ignore_case_local(args[0], "q") ||
        streq_ignore_case_local(args[0], "quit") ||
        streq_ignore_case_local(args[0], "exit")) {
        return 2;
    }
    if (streq_ignore_case_local(args[0], "help") || streq_ignore_case_local(args[0], "?")) {
        write_str("show | add <size> <type> [boot] | set <slot> <start> <size> <type> [boot]\n");
        write_str("clear <slot> | boot <slot> | resize <slot> <size> | wipe\n");
        write_str("gpt show | gpt init | gpt add <size> <type> [name] | gpt clear <slot> | gpt resize <slot> <size>\n");
        write_str("sizes: sectors, Ns, Nk, Nm, Ng, 100%\n");
        return 0;
    }
    if (streq_ignore_case_local(args[0], "show") || streq_ignore_case_local(args[0], "p")) {
        return fdisk_show_disk(disk_index);
    }
    if (arg_count + 2u > FDISK_SHELL_ARG_MAX + 3u) {
        write_err_str("fdisk: too many shell arguments\n");
        return 1;
    }
    fdisk_u32_to_text(disk_index, disk_text);
    fake[0] = "fdisk";
    fake[1] = disk_text;
    for (uint32_t i = 0u; i < arg_count; i++) {
        fake[i + 2u] = args[i];
    }
    if (streq_ignore_case_local(args[0], "set")) {
        return fdisk_command_set(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "add")) {
        return fdisk_command_add(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "resize")) {
        return fdisk_command_resize(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "clear")) {
        return fdisk_command_clear(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "boot")) {
        return fdisk_command_boot(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "wipe")) {
        return fdisk_command_wipe(disk_index, (int)arg_count + 2, fake);
    }
    if (streq_ignore_case_local(args[0], "gpt")) {
        return fdisk_command_gpt(disk_index, (int)arg_count + 2, fake);
    }
    write_err_str("fdisk: unknown shell command\n");
    return 1;
}

static int fdisk_command_shell(uint32_t disk_index) {
    char line[FDISK_SHELL_LINE_MAX];
    char *args[FDISK_SHELL_ARG_MAX];

    write_str("fdisk shell. type 'help' or 'quit'.\n");
    for (;;) {
        uint32_t argc;
        int rc;

        write_str("fdisk> ");
        if (!fdisk_shell_read_line(line, sizeof(line))) {
            write_str("\n");
            break;
        }
        argc = fdisk_shell_split(line, args, FDISK_SHELL_ARG_MAX);
        rc = fdisk_shell_execute(disk_index, args, argc);
        if (rc == 2) {
            break;
        }
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
        write_str("   or: fdisk <disk> add <sectors|+size|100%> <type> [boot]\n");
        write_str("   or: fdisk <disk> resize <slot 1..4> <sectors|+size|100%>\n");
        write_str("   or: fdisk <disk> clear <slot 1..4>\n");
        write_str("   or: fdisk <disk> boot <slot 1..4>\n");
        write_str("   or: fdisk <disk> wipe\n");
        write_str("   or: fdisk <disk> gpt [show|init|add|clear|resize]\n");
        write_str("   or: fdisk <disk> shell\n");
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
    if (streq_ignore_case_local(argv[2], "add")) {
        return fdisk_command_add(disk_index, argc, argv);
    }
    if (streq_ignore_case_local(argv[2], "resize")) {
        return fdisk_command_resize(disk_index, argc, argv);
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
    if (streq_ignore_case_local(argv[2], "gpt")) {
        return fdisk_command_gpt(disk_index, argc, argv);
    }
    if (streq_ignore_case_local(argv[2], "shell")) {
        return fdisk_command_shell(disk_index);
    }
    write_err_str("fdisk: unknown action\n");
    write_err_str("actions: set add resize clear boot wipe gpt shell\n");
    return 1;
}
