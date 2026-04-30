#pragma once

#include <stdint.h>
#include "kernel/public/sys/system_limits.h"

struct vfs;

struct fs_service_root_entry_info {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t native_id;
    uint32_t size;
    uint32_t attributes;
};

int fs_service_root_get_entry(struct vfs *vfs, uint32_t index, struct fs_service_root_entry_info *entry_out);
int fs_service_root_find_entry(struct vfs *vfs, const char *name, struct fs_service_root_entry_info *entry_out);
