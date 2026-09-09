#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

struct process;
struct vfs;

uint64_t fs_service_mkdir(struct process *proc, struct vfs *vfs, const char *path);
uint64_t fs_service_rmdir(struct process *proc, struct vfs *vfs, const char *path);
uint64_t fs_service_remove(struct process *proc, struct vfs *vfs, const char *path);
uint64_t fs_service_mkfifo(struct vfs *vfs, const char *path);
uint64_t fs_service_chmod(struct process *proc, struct vfs *vfs, const char *path, uint32_t mode);
uint64_t fs_service_chown(struct process *proc, struct vfs *vfs, const char *path, uint32_t uid, uint32_t gid);
uint64_t fs_service_setcap(struct process *proc, struct vfs *vfs, const char *path, uint32_t caps);
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
