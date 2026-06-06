#include "fs/vfs_internal.h"

static int64_t vfs_emit_dir_entry(struct vfs_dirent *entry,
                                  uint32_t *index_io,
                                  const char *name,
                                  uint32_t size,
                                  uint8_t attributes) {
    vfs_copy_name(entry->name, sizeof(entry->name), name);
    entry->size = size;
    entry->attributes = attributes;
    if (index_io != 0) {
        (*index_io)++;
    }
    return 1;
}

static int vfs_mountpoint_hidden_from_root(const struct vfs *vfs, uint8_t kind, uint32_t mount_slot) {
    return vfs_has_root_override(vfs) && vfs->root_kind == kind && vfs->root_slot == mount_slot;
}

static int64_t vfs_readdir_root_mountpoint(struct vfs *vfs,
                                           uint32_t *index_io,
                                           struct vfs_dirent *entry,
                                           uint32_t mountpoint_index) {
    struct vfs_mount_info info;
    uint32_t source_known = 0;
    uint32_t builtin_count = vfs_builtin_mount_count(vfs);
    uint32_t visible_index = 0;

    for (uint32_t i = 0; i < builtin_count; i++) {
        if (!vfs_get_builtin_mount(vfs, i, &info, &source_known) ||
            vfs_mountpoint_hidden_from_root(vfs, info.kind, 0u)) {
            continue;
        }
        if (visible_index == mountpoint_index) {
            (void)source_known;
            return vfs_emit_dir_entry(entry, index_io, info.name, 0, VFS_ATTR_DIR);
        }
        visible_index++;
    }
    for (uint32_t i = 0; i < vfs_mount_count(vfs); i++) {
        if (vfs_get_mount(vfs, i, &info) != 0 ||
            vfs_mountpoint_hidden_from_root(vfs, info.kind, i + 1u)) {
            continue;
        }
        if (visible_index == mountpoint_index) {
            return vfs_emit_dir_entry(entry, index_io, info.name, 0, VFS_ATTR_DIR);
        }
        visible_index++;
    }
    return 0;
}

static int64_t vfs_readdir_root_view_mountpoints(struct vfs *vfs,
                                                 uint32_t *index_io,
                                                 struct vfs_dirent *entry,
                                                 uint32_t native_count) {
    if (*index_io < native_count) {
        return 0;
    }
    return vfs_readdir_root_mountpoint(vfs, index_io, entry, *index_io - native_count);
}

int64_t vfs_read_from_fat32(struct vfs *vfs,
                            struct vfs_node *node,
                            uint32_t *offset_io,
                            void *buffer,
                            uint32_t size) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    uint32_t bytes_read = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_read_file_range(fat32, &node->handle.fat32_file, *offset_io, buffer, size, &bytes_read) != 0) {
        return -1;
    }
    *offset_io += bytes_read;
    return (int64_t)bytes_read;
}

int64_t vfs_read_from_nxfs(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           void *buffer,
                           uint32_t size) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    uint32_t bytes_read = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_read_file_range(nxfs, &node->handle.nxfs_inode, *offset_io, buffer, size, &bytes_read) != 0) {
        return -1;
    }
    *offset_io += bytes_read;
    return (int64_t)bytes_read;
}

int64_t vfs_write_to_fat32(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *offset_io,
                           const void *buffer,
                           uint32_t size) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    uint32_t bytes_written = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_write_file_range(fat32, &node->handle.fat32_file, *offset_io, buffer, size, &bytes_written) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    return (int64_t)bytes_written;
}

int64_t vfs_write_to_nxfs(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *offset_io,
                          const void *buffer,
                          uint32_t size) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    uint32_t bytes_written = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_write_file_range(nxfs,
                              node->aux_index,
                              &node->handle.nxfs_inode,
                              *offset_io,
                              buffer,
                              size,
                              &bytes_written) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    return (int64_t)bytes_written;
}

int vfs_prepare_fat32_opened_node(struct vfs *vfs,
                                  struct vfs_node *node,
                                  const char *path,
                                  uint32_t flags,
                                  uint32_t *offset_out) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;

    (void)path;
    if (vfs == 0 || node == 0 || offset_out == 0) {
        return -1;
    }
    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if ((flags & VFS_OPEN_TRUNCATE) != 0 &&
        node->handle.fat32_file.size != 0u &&
        fat32_truncate_file(fat32, &node->handle.fat32_file) != 0) {
        return -1;
    }
    if ((flags & VFS_OPEN_TRUNCATE) != 0) {
        vfs_event_file_change_emit("truncate",
                                   path,
                                   VFS_MOUNT_FAT32,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   0);
    }
    *offset_out = ((flags & VFS_OPEN_APPEND) != 0) ? node->handle.fat32_file.size : 0u;
    return 0;
}

int vfs_prepare_nxfs_opened_node(struct vfs *vfs,
                                 struct vfs_node *node,
                                 const char *path,
                                 uint32_t flags,
                                 uint32_t *offset_out) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (vfs == 0 || node == 0 || path == 0 || offset_out == 0) {
        return -1;
    }
    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if ((flags & VFS_OPEN_TRUNCATE) != 0 &&
        node->handle.nxfs_inode.size != 0u) {
        if (nxfs_truncate_inode(nxfs, node->aux_index, &node->handle.nxfs_inode) != 0) {
            return -1;
        }
    }
    if ((flags & VFS_OPEN_TRUNCATE) != 0) {
        vfs_event_file_change_emit("truncate",
                                   path,
                                   VFS_MOUNT_NXFS,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   0);
    }
    *offset_out = ((flags & VFS_OPEN_APPEND) != 0) ? node->handle.nxfs_inode.size : 0u;
    return 0;
}

static int64_t vfs_readdir_root(struct vfs *vfs, uint32_t *index_io, struct vfs_dirent *entry) {
    return vfs_readdir_root_view_mountpoints(vfs, index_io, entry, 0);
}

uint32_t vfs_node_file_size(const struct vfs_node *node) {
    if (node == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_FAT32) {
        return node->handle.fat32_file.size;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        return node->handle.nxfs_inode.size;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_devfs_file_size(node);
    }
    return 0;
}

int64_t vfs_read_dir_fat32(struct vfs *vfs,
                           struct vfs_node *node,
                           uint32_t *index_io,
                           struct vfs_dirent *entry) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    struct fat32_file fat32_file;
    uint32_t native_count = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, node->mount_slot, &mount)) {
        return 0;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_get_dir_entry(fat32, &node->handle.fat32_file, *index_io, &fat32_file) == 0) {
        return vfs_emit_dir_entry(entry, index_io, fat32_file.name, fat32_file.size, fat32_file.attributes);
    }
    if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) == 0) {
        return 0;
    }
    while (fat32_get_dir_entry(fat32, &node->handle.fat32_file, native_count, &fat32_file) == 0) {
        native_count++;
    }
    return vfs_readdir_root_view_mountpoints(vfs, index_io, entry, native_count);
}

int64_t vfs_read_dir_nxfs(struct vfs *vfs,
                          struct vfs_node *node,
                          uint32_t *index_io,
                          struct vfs_dirent *entry) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    struct nxfs_dir_entry nxfs_entry;
    struct nxfs_inode nxfs_inode;
    uint32_t count = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, node->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_list_dir(nxfs, node->aux_index, &node->handle.nxfs_inode, &nxfs_entry, 1, &count) != 0) {
        return -1;
    }
    if (*index_io >= count) {
        if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) == 0) {
            return 0;
        }
        return vfs_readdir_root_view_mountpoints(vfs, index_io, entry, count);
    }
    if (nxfs_get_dir_entry(nxfs, node->aux_index, &node->handle.nxfs_inode, *index_io, &nxfs_entry) != 0) {
        return 0;
    }
    vfs_emit_dir_entry(entry, 0, nxfs_entry.name, 0, 0);
    if (nxfs_read_inode(nxfs, nxfs_entry.inode, &nxfs_inode) == 0) {
        entry->size = nxfs_inode.size;
        if (nxfs_inode.type == NXFS_TYPE_DIR) {
            entry->attributes = VFS_ATTR_DIR;
        }
    }
    (*index_io)++;
    return 1;
}

static int vfs_can_access_file_node(const struct vfs *vfs, const struct vfs_node *node) {
    if (vfs == 0 || node == 0 || node->kind != VFS_NODE_FILE) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS ||
        node->mount_kind == VFS_MOUNT_PROCFS ||
        node->mount_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    return vfs_mount_ready(vfs, node->mount_kind);
}

static int vfs_can_access_dir_node(const struct vfs *vfs, const struct vfs_node *node) {
    if (vfs == 0 || node == 0 || node->kind != VFS_NODE_DIR) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_ROOT ||
        node->mount_kind == VFS_MOUNT_DEVFS ||
        node->mount_kind == VFS_MOUNT_PROCFS ||
        node->mount_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    return vfs_mount_ready(vfs, node->mount_kind);
}

int64_t vfs_read(struct vfs *vfs,
                 struct vfs_node *node,
                 uint32_t *offset_io,
                 void *buffer,
                 uint32_t size,
                 uint32_t flags) {
    const struct vfs_mount_ops *ops;

    if (!vfs_can_access_file_node(vfs, node) || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_read_from_devfs(vfs, node, offset_io, buffer, size, flags);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS) {
        (void)flags;
        return vfs_read_from_procfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_EVENTFS) {
        (void)flags;
        return vfs_read_from_eventfs(vfs, node, offset_io, buffer, size);
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->read_file != 0 ? ops->read_file(vfs, node, offset_io, buffer, size) : -1;
}

int64_t vfs_write(struct vfs *vfs,
                  struct vfs_node *node,
                  uint32_t *offset_io,
                  const void *buffer,
                  uint32_t size,
                  const char *opened_path) {
    const struct vfs_mount_ops *ops;
    int64_t written;

    if (!vfs_can_access_file_node(vfs, node) || offset_io == 0 || buffer == 0 || size == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_write_to_devfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS || node->mount_kind == VFS_MOUNT_EVENTFS) {
        return -1;
    }
    ops = vfs_mount_ops(node->mount_kind);
    written = ops != 0 && ops->write_file != 0 ? ops->write_file(vfs, node, offset_io, buffer, size) : -1;
    if (written > 0) {
        vfs_event_file_change_emit("write",
                                   opened_path,
                                   node->mount_kind,
                                   node->mount_slot,
                                   vfs_node_native_id(node),
                                   (uint32_t)written);
    }
    return written;
}

int64_t vfs_readdir(struct vfs *vfs,
                    struct vfs_node *node,
                    uint32_t *index_io,
                    struct vfs_dirent *entry) {
    const struct vfs_mount_ops *ops;

    if (!vfs_can_access_dir_node(vfs, node) || index_io == 0 || entry == 0) {
        return -1;
    }
    if (node->mount_kind == VFS_MOUNT_ROOT) {
        return vfs_readdir_root(vfs, index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_read_dir_devfs(index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_read_dir_procfs(node, index_io, entry);
    }
    if (node->mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_read_dir_eventfs(node, index_io, entry);
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->read_dir != 0 ? ops->read_dir(vfs, node, index_io, entry) : -1;
}

int vfs_prepare_opened_node(struct vfs *vfs,
                            struct vfs_node *node,
                            const char *path,
                            uint32_t flags,
                            uint32_t *offset_out) {
    const struct vfs_mount_ops *ops;

    if (offset_out == 0) {
        return -1;
    }
    *offset_out = 0;
    if (node == 0) {
        return -1;
    }
    ops = vfs_mount_ops(node->mount_kind);
    return ops != 0 && ops->prepare_opened_node != 0
               ? ops->prepare_opened_node(vfs, node, path, flags, offset_out)
               : 0;
}

int vfs_read_file_all(struct vfs *vfs,
                      const char *path,
                      struct vfs_node *node_out,
                      void *buffer,
                      uint32_t buffer_size,
                      uint32_t *bytes_read_out) {
    struct vfs_node node;
    uint32_t offset = 0;
    uint32_t file_size;
    int64_t read_rc;

    if (vfs == 0 || path == 0 || buffer == 0 || bytes_read_out == 0) {
        return -1;
    }
    *bytes_read_out = 0;
    if (vfs_open(vfs, path, 0, &node) != 0 || node.kind != VFS_NODE_FILE) {
        return -1;
    }
    file_size = vfs_node_file_size(&node);
    if (file_size > buffer_size) {
        return -1;
    }
    read_rc = vfs_read(vfs, &node, &offset, buffer, file_size, VFS_READ_BLOCKING);
    if (read_rc < 0 || (uint32_t)read_rc != file_size) {
        return -1;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    *bytes_read_out = file_size;
    return 0;
}
