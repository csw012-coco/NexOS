#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

struct file;
struct process;
struct vfs;

uint64_t fs_service_read(struct process *proc,
                         struct vfs *vfs,
                         uint32_t fd,
                         void *buffer,
                         uint32_t size,
                         uint32_t flags,
                         uint32_t *copied_out);
uint64_t fs_service_write(struct process *proc,
                          struct vfs *vfs,
                          uint32_t fd,
                          const void *buffer,
                          uint32_t size);
uint64_t fs_service_close(struct process *proc, uint32_t fd);
uint64_t fs_service_dup2(struct process *proc, uint32_t src_fd, uint32_t dst_fd);
uint64_t fs_service_pipe(struct process *proc, uint32_t pair_out[2]);
uint64_t fs_service_readdir(struct process *proc,
                            struct vfs *vfs,
                            uint32_t fd,
                            struct syscall_dirent *entry);
