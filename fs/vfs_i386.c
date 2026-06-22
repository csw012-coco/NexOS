#include "fs/vfs_internal.h"

static const char *i386_vfs_path(const char *path) {
    if (path == 0) {
        return 0;
    }
    while (*path == '/') {
        path++;
    }
    return path;
}

int vfs_open(struct vfs *vfs, const char *path, uint32_t flags, struct vfs_node *out) {
    struct fat32_file file;
    const char *relative = i386_vfs_path(path);
    int found;

    if (vfs == 0 || out == 0 || relative == 0 || relative[0] == '\0' ||
        (flags & ~(VFS_OPEN_CREATE |
                   VFS_OPEN_TRUNCATE |
                   VFS_OPEN_APPEND)) != 0u ||
        vfs->root_kind != VFS_MOUNT_FAT32) {
        return -1;
    }
    found = fat32_find_path(&vfs->fat32, relative, &file) == 0;
    if (!found) {
        if ((flags & VFS_OPEN_CREATE) == 0u ||
            fat32_create_path(&vfs->fat32, relative, &file) != 0) {
            return -1;
        }
    }
    if ((file.attributes & VFS_ATTR_DIR) != 0u) {
        return -1;
    }
    if ((flags & VFS_OPEN_TRUNCATE) != 0u &&
        fat32_truncate_file(&vfs->fat32, &file) != 0) {
        return -1;
    }
    vfs_set_fat32_file_node(out, 0u, &file);
    return 0;
}

int vfs_opendir(struct vfs *vfs, const char *path, struct vfs_node *out) {
    struct fat32_file directory;
    const char *relative = i386_vfs_path(path);

    if (vfs == 0 || out == 0 || relative == 0 ||
        vfs->root_kind != VFS_MOUNT_FAT32) {
        return -1;
    }
    if (relative[0] == '\0') {
        fat32_get_root_dir(&vfs->fat32, &directory);
    } else if (fat32_find_path(&vfs->fat32, relative, &directory) != 0 ||
               (directory.attributes & VFS_ATTR_DIR) == 0u) {
        return -1;
    }
    vfs_set_fat32_dir_node(out, 0u, &directory);
    return 0;
}

int64_t vfs_read(struct vfs *vfs,
                 struct vfs_node *node,
                 uint32_t *offset_io,
                 void *buffer,
                 uint32_t size,
                 uint32_t flags) {
    uint32_t bytes_read = 0;

    (void)flags;
    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0 ||
        node->kind != VFS_NODE_FILE ||
        node->mount_kind != VFS_MOUNT_FAT32 ||
        fat32_read_file_range(&vfs->fat32,
                              &node->handle.fat32_file,
                              *offset_io,
                              buffer,
                              size,
                              &bytes_read) != 0) {
        return -1;
    }
    *offset_io += bytes_read;
    return bytes_read;
}

int64_t vfs_write(struct vfs *vfs,
                  struct vfs_node *node,
                  uint32_t *offset_io,
                  const void *buffer,
                  uint32_t size,
                  const char *opened_path) {
    uint32_t bytes_written = 0u;

    (void)opened_path;
    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0 ||
        node->kind != VFS_NODE_FILE ||
        node->mount_kind != VFS_MOUNT_FAT32 ||
        fat32_write_file_range(&vfs->fat32,
                               &node->handle.fat32_file,
                               *offset_io,
                               buffer,
                               size,
                               &bytes_written) != 0 ||
        blockdev_flush(vfs->fat32.bdev) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    return (int64_t)bytes_written;
}

int64_t vfs_readdir(struct vfs *vfs,
                    struct vfs_node *node,
                    uint32_t *index_io,
                    struct vfs_dirent *entry) {
    struct fat32_file file;

    if (vfs == 0 || node == 0 || index_io == 0 || entry == 0 ||
        node->kind != VFS_NODE_DIR ||
        node->mount_kind != VFS_MOUNT_FAT32) {
        return -1;
    }
    if (fat32_get_dir_entry(&vfs->fat32,
                            &node->handle.fat32_file,
                            *index_io,
                            &file) != 0) {
        return 0;
    }

    vfs_copy_name(entry->name, sizeof(entry->name), file.name);
    entry->size = file.size;
    entry->attributes = file.attributes;
    (*index_io)++;
    return 1;
}
