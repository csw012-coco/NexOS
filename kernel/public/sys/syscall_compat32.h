#pragma once

#include <stdint.h>

#include "abi/syscall_abi.h"
#include "drivers/input/keyboard.h"
#include "kernel/public/sys/syscall_request.h"

struct block_device;
struct blockdev_partition;
struct process_context;
struct process_snapshot;
struct syscall_compat32_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));
struct tty;
struct vfs;

struct syscall_compat32_context {
    struct vfs *vfs;
    struct tty *tty;
    char *io_buffer;
    uint32_t io_buffer_size;
    uint32_t pid;
    uint32_t ticks;

    const struct syscall_boot_info *boot_info;
    const struct syscall_framebuffer_info *fb_info;
    const struct syscall_compat32_memmap_entry *memmap;
    uint32_t memmap_count;

    int (*pop_keyboard_event)(struct keyboard_event *event);
    int32_t (*open)(struct vfs *vfs, const char *path, uint32_t flags);
    int32_t (*read)(struct vfs *vfs,
                    uint32_t fd,
                    void *buffer,
                    uint32_t size,
                    uint32_t flags);
    int32_t (*write)(struct vfs *vfs,
                     uint32_t fd,
                     const void *buffer,
                     uint32_t size);
    int32_t (*close)(uint32_t fd);
    int32_t (*seek)(uint32_t fd, int32_t offset, uint32_t whence);
    uint32_t (*page_alloc)(void);
    uint32_t (*page_alloc_prot)(int writable);
    uint32_t (*page_alloc_at)(uint32_t user_page, int writable);
    int32_t (*page_protect)(uint32_t user_page, int writable);
    int32_t (*page_free)(uint32_t user_page);
    uint32_t (*shared_page_alloc)(void);
    int32_t (*shared_page_free)(uint32_t frame);
    uint32_t (*shared_page_map)(uint32_t frame);
    int32_t (*shared_page_unmap)(uint32_t user_page);
    int32_t (*spawn_command)(const char *command,
                             uint32_t mode,
                             uint32_t flags);
    uintptr_t (*wait)(const struct process_context *context,
                      uint32_t pid,
                      int32_t *status,
                      int *blocked);
    uintptr_t (*exit)(const struct process_context *context,
                      int exit_code);
    uintptr_t (*yield)(const struct process_context *context);
    uintptr_t (*sleep)(const struct process_context *context,
                       uint32_t ticks);
    int32_t (*chdir)(struct vfs *vfs, const char *path);
    int32_t (*getcwd)(char *buffer, uint32_t size);
    int32_t (*opendir)(struct vfs *vfs, const char *path);
    int32_t (*readdir)(struct vfs *vfs,
                       uint32_t fd,
                       struct syscall_dirent *entry);
    int32_t (*mkdir)(struct vfs *vfs, const char *path);
    int32_t (*rmdir)(struct vfs *vfs, const char *path);
    int32_t (*remove)(struct vfs *vfs, const char *path);
    int32_t (*pipe)(uint32_t pair[2]);
    int32_t (*dup2)(uint32_t src_fd, uint32_t dst_fd);
    int32_t (*kill)(uint32_t pid);
    int (*process_snapshot)(uint32_t slot, struct process_snapshot *out);
    uint32_t (*fd_kind)(uint32_t fd);
    int32_t (*fd_query)(uint32_t fd, struct syscall_fd_info *info);

    void (*fill_machine_info)(struct syscall_machine_info *info);
    void (*fill_block_info)(struct syscall_block_info *info,
                            uint32_t index,
                            struct block_device *dev);
    void (*fill_part_info)(struct syscall_partition_info *info,
                           uint32_t disk_index,
                           uint32_t part_index,
                           const struct blockdev_partition *part);
    int (*fill_mount_info)(struct syscall_mount_info *info, uint32_t index);
    uint32_t (*pmm_total_pages)(void);
    uint32_t (*pmm_free_pages)(void);
    uint32_t (*pmm_reserved_pages)(void);
};

int syscall_compat32_dispatch_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
