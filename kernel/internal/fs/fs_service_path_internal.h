#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

struct process;
struct vfs;

uint64_t fs_service_mkdir(struct vfs *vfs, const char *path);
uint64_t fs_service_rmdir(struct vfs *vfs, const char *path);
uint64_t fs_service_remove(struct vfs *vfs, const char *path);
uint64_t fs_service_mount(struct vfs *vfs, const char *source, const char *target, uint32_t syscall_kind);
uint64_t fs_service_mount_boot(struct vfs *vfs,
                               const char *target,
                               uint32_t syscall_kind,
                               uint32_t partition_lba,
                               uint32_t partition_sectors);
uint64_t fs_service_umount(struct vfs *vfs, const char *target);
uint64_t fs_service_switch_root(struct vfs *vfs, const char *target);
uint64_t fs_service_open(struct process *proc, struct vfs *vfs, const char *path, uint32_t flags);
uint64_t fs_service_opendir(struct process *proc, struct vfs *vfs, const char *path);
