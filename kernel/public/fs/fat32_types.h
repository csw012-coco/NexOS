#pragma once

#include <stdint.h>
#include "kernel/public/sys/system_limits.h"

struct fat32_file {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t first_cluster;
    uint32_t size;
    uint8_t attributes;
    uint8_t lfn_entry_count;
    uint16_t reserved;
    uint32_t dirent_index;
    uint32_t dirent_lba;
    uint32_t dirent_offset;
};
