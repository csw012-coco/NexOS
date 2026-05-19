#pragma once

#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static inline void storage_write_u64_dec(uint64_t value) {
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

static inline int parse_mount_kind_local(const char *text, uint32_t *kind_out) {
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

static inline const char *cmd_mount_error_message(int rc) {
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
