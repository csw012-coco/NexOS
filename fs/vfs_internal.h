#pragma once

#include "fs/vfs.h"
#include "fs/fat32.h"
#include "fs/nxfs.h"
#include "block/blockdev.h"
#include "kernel/public/sys/syscall.h"

enum {
    VFS_ATTR_DIR = 0x10u,
    VFS_NODE_FLAG_ROOT_VIEW = 0x1u,
    VFS_PROC_ROOT = 1u,
    VFS_PROC_MEMINFO = 2u,
    VFS_PROC_MOUNTS = 3u,
    VFS_PROC_UPTIME = 4u,
    VFS_PROC_KMSG = 5u,
    VFS_PROC_ACTIONS = 6u,
    VFS_PROC_RTC = 7u,
    VFS_PROC_CAPS = 8u,
    VFS_PROC_DEVICES = 9u,
    VFS_PROC_DRIVERS = 10u,
    VFS_PROC_PID_DIR = 11u,
    VFS_PROC_PID_STATUS = 12u,
    VFS_EVENT_ROOT = 1u,
    VFS_EVENT_TIMER = 2u,
    VFS_EVENT_INPUT_DIR = 3u,
    VFS_EVENT_INPUT_KEYBOARD = 4u,
    VFS_EVENT_NET_DIR = 5u,
    VFS_EVENT_NET_STATUS = 6u,
    VFS_EVENT_FILE_DIR = 7u,
    VFS_EVENT_FILE_CHANGE = 8u,
    VFS_EVENT_TIMER_JSON = 9u,
    VFS_EVENT_INPUT_KEYBOARD_JSON = 10u,
    VFS_EVENT_NET_STATUS_JSON = 11u,
    VFS_EVENT_FILE_CHANGE_JSON = 12u,
    VFS_EVENT_INPUT_MOUSE = 13u,
    VFS_EVENT_INPUT_MOUSE_JSON = 14u,
    VFS_EVENT_BLOCK_DIR = 15u,
    VFS_EVENT_BLOCK_CHANGE = 16u,
    VFS_EVENT_BLOCK_CHANGE_JSON = 17u,
    VFS_EVENT_SECURITY_DIR = 18u,
    VFS_EVENT_SECURITY_CAPABILITY = 19u,
    VFS_EVENT_SECURITY_CAPABILITY_JSON = 20u,
    VFS_DEV_MAJOR_TTY = 4u,
    VFS_DEV_MAJOR_MISC = 5u,
    VFS_DEV_MAJOR_BLOCK = 8u,
    VFS_DEV_MAJOR_FRAMEBUFFER = 29u,
    VFS_DEV_TTY = 1u,
    VFS_DEV_NULL = 2u,
    VFS_DEV_ZERO = 3u,
    VFS_DEV_STDIN = 4u,
    VFS_DEV_STDOUT = 5u,
    VFS_DEV_STDERR = 6u,
    VFS_DEV_BLOCK_DEVICE = 7u,
    VFS_DEV_BLOCK_PARTITION = 8u,
    VFS_DEV_FRAMEBUFFER = 9u,
    VFS_DEV_TTYS0 = 10u,
    VFS_DEV_TTY2 = 11u,
    VFS_DEV_TTY3 = 12u,
    VFS_DEV_BLOCK_BUFFER_SIZE = 512u,
    VFS_PROC_TEXT_BUFFER_SIZE = 4096u,
    VFS_EVENT_TEXT_BUFFER_SIZE = 2048u
};

struct vfs_path {
    uint8_t root_dir;
    uint8_t has_child;
    uint8_t child_is_root;
    uint8_t mount_kind;
    uint32_t mount_slot;
    char child[NOS_PATH_BUFFER_SIZE];
};

struct vfs_builtin_mount_info {
    uint8_t kind;
    const char *name;
    struct block_device *bdev;
    uint32_t partition_lba;
};

struct vfs_builtin_mount_provider {
    uint8_t kind;
    uint8_t requires_partition;
    const char *name;
    const char *root_target;
    int (*probe_source)(struct block_device *dev, uint32_t partition_lba);
    int (*mount_builtin)(struct vfs *vfs, struct block_device *dev, uint32_t partition_lba);
    int (*mount_dynamic)(struct vfs *vfs, uint32_t slot, struct block_device *dev, uint32_t partition_lba);
    int (*is_builtin_mounted)(const struct vfs *vfs);
    int (*fill_builtin_info)(const struct vfs *vfs, struct vfs_builtin_mount_info *out);
};

struct vfs_mount_instance {
    uint8_t kind;
    uint32_t mount_slot;
    void *fs_data;
    struct block_device *bdev;
    uint32_t partition_lba;
};

struct vfs_proc_action_entry {
    const char *name;
    const char *group;
    const char *command;
    const char *input_schema;
    const char *output_schema;
    uint32_t cap_flags;
    const char *caps;
    const char *summary;
};

struct vfs_proc_cap_entry {
    const char *name;
    uint32_t flag;
    const char *summary;
};

struct vfs_mount_ops {
    int (*open_file)(struct vfs *vfs, const struct vfs_path *parsed, uint32_t flags, struct vfs_node *out);
    int (*open_dir)(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out);
    int (*open_mount_root)(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out);
    int (*mkdir_path)(struct vfs *vfs, const struct vfs_path *parsed);
    int (*rmdir_path)(struct vfs *vfs, const struct vfs_path *parsed);
    int (*unlink_path)(struct vfs *vfs, const struct vfs_path *parsed);
    int64_t (*read_file)(struct vfs *vfs, struct vfs_node *node, uint32_t *offset_io, void *buffer, uint32_t size);
    int64_t (*write_file)(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *offset_io,
                          const void *buffer,
                          uint32_t size);
    int64_t (*read_dir)(struct vfs *vfs, struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry);
    int (*prepare_opened_node)(struct vfs *vfs,
                               struct vfs_node *node,
                               const char *path,
                               uint32_t flags,
                               uint32_t *offset_out);
};

struct vfs {
    struct fat32_volume fat32;
    struct nxfs_volume nxfs;
    uint8_t root_kind;
    uint32_t root_slot;
    uint8_t devfs_block_buffer[VFS_DEV_BLOCK_BUFFER_SIZE];
    char procfs_text[VFS_PROC_TEXT_BUFFER_SIZE];
    char eventfs_text[VFS_EVENT_TEXT_BUFFER_SIZE];
    uint32_t eventfs_text_size;
    uint32_t eventfs_text_node;
    struct {
        uint8_t used;
        uint8_t kind;
        uint32_t disk_index;
        uint32_t part_index;
        char name[NOS_NAME_BUFFER_SIZE];
        struct fat32_volume fat32;
        struct nxfs_volume nxfs;
    } mounts[VFS_MOUNT_SLOT_MAX];
};

int vfs_has_root_override(const struct vfs *vfs);
void vfs_copy_name(char *dst, uint32_t dst_size, const char *src);
void vfs_format_disk_node_name(char *dst, uint32_t dst_size, uint32_t disk_index);
void vfs_format_partition_node_name(char *dst,
                                    uint32_t dst_size,
                                    uint32_t disk_index,
                                    uint32_t part_index);
int vfs_contains_char(const char *text, char needle);
uint32_t vfs_format_procfs_node(struct vfs *vfs, struct vfs_node *node, char *text, uint32_t size);
int vfs_procfs_pid_exists(uint32_t pid);
uint32_t vfs_format_eventfs_node(struct vfs_node *node, char *text, uint32_t size);
int vfs_parse_path_for_vfs(const struct vfs *vfs, const char *path, struct vfs_path *out);
int vfs_resolve_mount_target_name(const struct vfs *vfs, const char *target, char *name, uint32_t name_size);
int vfs_find_dynamic_mount(const struct vfs *vfs, const char *name, uint32_t *slot_out);
int vfs_mount_ready(const struct vfs *vfs, uint8_t mount_kind);
int vfs_get_builtin_mount_info(const struct vfs *vfs, uint32_t index, struct vfs_builtin_mount_info *out);
uint32_t vfs_builtin_mount_provider_count(void);
const struct vfs_builtin_mount_provider *vfs_builtin_mount_provider_at(uint32_t index);
const struct vfs_builtin_mount_provider *vfs_builtin_mount_provider(uint8_t kind);
uint32_t vfs_proc_action_count(void);
const struct vfs_proc_action_entry *vfs_proc_action_at(uint32_t index);
uint32_t vfs_proc_cap_count(void);
const struct vfs_proc_cap_entry *vfs_proc_cap_at(uint32_t index);
const struct vfs_mount_ops *vfs_mount_ops(uint8_t mount_kind);
int vfs_open_fat32(struct vfs *vfs, const struct vfs_path *parsed, uint32_t flags, struct vfs_node *out);
int vfs_open_nxfs(struct vfs *vfs, const struct vfs_path *parsed, uint32_t flags, struct vfs_node *out);
int vfs_opendir_mount_root(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out);
int vfs_opendir_fat32(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out);
int vfs_opendir_nxfs(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out);
int vfs_mkdir_fat32(struct vfs *vfs, const struct vfs_path *parsed);
int vfs_mkdir_nxfs(struct vfs *vfs, const struct vfs_path *parsed);
int vfs_rmdir_fat32(struct vfs *vfs, const struct vfs_path *parsed);
int vfs_rmdir_nxfs(struct vfs *vfs, const struct vfs_path *parsed);
int vfs_unlink_fat32(struct vfs *vfs, const struct vfs_path *parsed);
int vfs_unlink_nxfs(struct vfs *vfs, const struct vfs_path *parsed);
int64_t vfs_read_from_fat32(struct vfs *vfs, struct vfs_node *node, uint32_t *offset_io, void *buffer, uint32_t size);
int64_t vfs_read_from_nxfs(struct vfs *vfs, struct vfs_node *node, uint32_t *offset_io, void *buffer, uint32_t size);
int64_t vfs_write_to_fat32(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           const void *buffer,
                           uint32_t size);
int64_t vfs_write_to_nxfs(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *offset_io,
                          const void *buffer,
                          uint32_t size);
void vfs_event_file_change_emit(const char *op,
                                const char *path,
                                uint8_t mount_kind,
                                uint32_t mount_slot,
                                uint32_t native_id,
                                uint32_t bytes);
void vfs_event_capability_emit(const struct syscall_capability_event *event);
int64_t vfs_read_dir_fat32(struct vfs *vfs, struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry);
int64_t vfs_read_dir_nxfs(struct vfs *vfs, struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry);
int vfs_prepare_fat32_opened_node(struct vfs *vfs,
                                  struct vfs_node *node,
                                  const char *path,
                                  uint32_t flags,
                                  uint32_t *offset_out);
int vfs_prepare_nxfs_opened_node(struct vfs *vfs,
                                 struct vfs_node *node,
                                 const char *path,
                                 uint32_t flags,
                                 uint32_t *offset_out);
int vfs_get_mount_instance(struct vfs *vfs,
                           uint8_t mount_kind,
                           uint32_t mount_slot,
                           struct vfs_mount_instance *out);
int vfs_get_root_mount_instance(struct vfs *vfs, struct vfs_mount_instance *out);
void vfs_set_dir_node(struct vfs_node *node, uint8_t mount_kind);
void vfs_set_fat32_dir_node(struct vfs_node *node, uint32_t mount_slot, const struct fat32_file *dir);
void vfs_set_fat32_file_node(struct vfs_node *node, uint32_t mount_slot, const struct fat32_file *file);
void vfs_set_nxfs_file_node(struct vfs_node *node,
                            uint32_t mount_slot,
                            uint32_t inode_index,
                            const struct nxfs_inode *inode);
void vfs_set_devfs_node(struct vfs_node *node, uint8_t kind, uint32_t dev_id);
void vfs_set_procfs_node(struct vfs_node *node, uint8_t kind, uint32_t proc_id, uint32_t aux_data);
void vfs_set_eventfs_node(struct vfs_node *node, uint8_t kind, uint32_t event_id);
void vfs_set_node_device_numbers(struct vfs_node *node, uint32_t major, uint32_t minor);
int vfs_devfs_lookup(const char *name, struct vfs_node *out);
int vfs_procfs_lookup(const char *name, struct vfs_node *out);
int vfs_procfs_opendir(const char *name, struct vfs_node *out);
int vfs_eventfs_lookup(const char *name, struct vfs_node *out);
int vfs_eventfs_opendir(const char *name, struct vfs_node *out);
int64_t vfs_read_from_procfs(struct vfs *vfs,
                             struct vfs_node *node,
                             uint32_t *offset_io,
                             void *buffer,
                             uint32_t size);
int64_t vfs_read_from_eventfs(struct vfs *vfs,
                              struct vfs_node *node,
                              uint32_t *offset_io,
                              void *buffer,
                              uint32_t size);
int64_t vfs_read_dir_procfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry);
int64_t vfs_read_dir_eventfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry);
int64_t vfs_read_from_devfs(struct vfs *vfs,
                            struct vfs_node *node,
                            uint32_t *offset_io,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags);
int64_t vfs_write_to_devfs(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           const void *buffer,
                           uint32_t size);
int64_t vfs_read_dir_devfs(uint32_t *index_io, struct vfs_dirent *entry);
uint32_t vfs_devfs_file_size(const struct vfs_node *node);
struct block_device *vfs_blockdev_from_node(const struct vfs_node *node,
                                            uint64_t *base_lba_out,
                                            uint64_t *block_count_out);
int64_t vfs_blockdev_read_bytes(struct vfs *vfs,
                                struct block_device *dev,
                                uint64_t base_lba,
                                uint64_t block_count,
                                uint32_t *offset_io,
                                void *buffer,
                                uint32_t size);
int64_t vfs_blockdev_write_bytes(struct vfs *vfs,
                                 struct block_device *dev,
                                 uint64_t base_lba,
                                 uint64_t block_count,
                                 uint32_t *offset_io,
                                 const void *buffer,
                                 uint32_t size);
