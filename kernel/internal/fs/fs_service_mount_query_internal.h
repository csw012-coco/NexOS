#pragma once

#include "fs/vfs.h"
#include "kernel/public/fs/vfs_types.h"
#include "kernel/public/sys/syscall.h"

int fs_service_get_mount_info(struct vfs *vfs, uint32_t index, struct vfs_mount_info *info_out);
int fs_service_fill_builtin_mount_info(struct vfs *vfs,
                                       uint32_t index,
                                       struct syscall_mount_info *info,
                                       uint32_t *offset_out);
