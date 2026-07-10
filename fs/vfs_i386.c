#include "fs/vfs_internal.h"
#include "lib/string.h"

static int i386_vfs_pseudo_path(const char *path, uint8_t *mount_kind_out, const char **child_out) {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t i = 0u;

    if (path == 0 || mount_kind_out == 0 || child_out == 0) {
        return 0;
    }
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return 0;
    }
    while (path[i] != '\0' && path[i] != '/') {
        if (i + 1u >= sizeof(name)) {
            return 0;
        }
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    if (streq(name, "dev")) {
        *mount_kind_out = VFS_MOUNT_DEVFS;
    } else if (streq(name, "proc")) {
        *mount_kind_out = VFS_MOUNT_PROCFS;
    } else if (streq(name, "event")) {
        *mount_kind_out = VFS_MOUNT_EVENTFS;
    } else {
        return 0;
    }
    path += i;
    while (*path == '/') {
        path++;
    }
    *child_out = path;
    return 1;
}

static int i386_vfs_ascii_streq_nocase(const char *lhs, const char *rhs) {
    uint32_t i = 0u;

    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        char a = lhs[i];
        char b = rhs[i];

        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        i++;
    }
    return lhs[i] == '\0' && rhs[i] == '\0';
}

static int i386_vfs_cmd_alias_path(const char *path, char *out, uint32_t out_size) {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t i = 0u;
    uint32_t pos = 0u;

    if (path == 0 || out == 0 || out_size == 0u) {
        return 0;
    }
    while (*path == '/') {
        path++;
    }
    while (path[i] != '\0' && path[i] != '/') {
        if (i + 1u >= sizeof(name)) {
            return 0;
        }
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    if (!i386_vfs_ascii_streq_nocase(name, "cmd")) {
        return 0;
    }
    path += i;
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        vfs_copy_name(out, out_size, "CMD");
        return 1;
    }
    if (i386_vfs_ascii_streq_nocase(path, "ush")) {
        vfs_copy_name(out, out_size, "BOOT/USH32.ELF");
        return 1;
    }
    if (i386_vfs_ascii_streq_nocase(path, "nexbox") ||
        i386_vfs_ascii_streq_nocase(path, "nexbox32")) {
        vfs_copy_name(out, out_size, "BOOT/NEXBOX32.ELF");
        return 1;
    }
    if (i386_vfs_ascii_streq_nocase(path, "nexbox32s")) {
        vfs_copy_name(out, out_size, "BOOT/NEXBOX32S.ELF");
        return 1;
    }
    if (out_size < 5u) {
        return 0;
    }
    out[pos++] = 'C';
    out[pos++] = 'M';
    out[pos++] = 'D';
    out[pos++] = '/';
    while (*path != '\0') {
        char ch = *path++;

        if (ch == '/') {
            return 0;
        }
        if (pos + 1u >= out_size) {
            return 0;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        out[pos++] = ch;
    }
    out[pos] = '\0';
    return 1;
}

static int i386_vfs_open_pseudo(uint8_t mount_kind, const char *child, uint32_t flags, struct vfs_node *out) {
    if (child == 0 || child[0] == '\0' || (flags & VFS_OPEN_CREATE) != 0u) {
        return -1;
    }
    if (mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_devfs_lookup(child, out);
    }
    if (mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_procfs_lookup(child, out);
    }
    if (mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_eventfs_lookup(child, out);
    }
    return -1;
}

static int i386_vfs_opendir_pseudo(uint8_t mount_kind, const char *child, struct vfs_node *out) {
    if (child == 0 || out == 0) {
        return -1;
    }
    if (child[0] == '\0') {
        if (mount_kind == VFS_MOUNT_DEVFS) {
            vfs_set_devfs_node(out, VFS_NODE_DIR, 0u);
            return 0;
        }
        if (mount_kind == VFS_MOUNT_PROCFS) {
            vfs_set_procfs_node(out, VFS_NODE_DIR, VFS_PROC_ROOT, 0u);
            return 0;
        }
        if (mount_kind == VFS_MOUNT_EVENTFS) {
            vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_ROOT);
            return 0;
        }
    }
    if (mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_procfs_opendir(child, out);
    }
    if (mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_eventfs_opendir(child, out);
    }
    return -1;
}

static struct fat32_volume *i386_vfs_resolve_fat32(struct vfs *vfs,
                                                   const char *path,
                                                   const char **relative_out,
                                                   uint32_t *mount_slot_out) {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t i = 0u;
    uint32_t slot;
    const char *relative;

    if (vfs == 0 || path == 0 || relative_out == 0 || mount_slot_out == 0) {
        return 0;
    }
    while (*path == '/') {
        path++;
    }
    if (path[0] == '\0') {
        *relative_out = path;
        *mount_slot_out = 0u;
        return &vfs->fat32;
    }
    while (path[i] != '\0' && path[i] != '/') {
        if (i + 1u >= sizeof(name)) {
            return 0;
        }
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    if (vfs_find_dynamic_mount(vfs, name, &slot) &&
        vfs->mounts[slot].kind == VFS_MOUNT_FAT32) {
        relative = path + i;
        while (*relative == '/') {
            relative++;
        }
        *relative_out = relative;
        *mount_slot_out = slot + 1u;
        return &vfs->mounts[slot].fat32;
    }
    *relative_out = path;
    *mount_slot_out = 0u;
    return &vfs->fat32;
}

static struct fat32_volume *i386_vfs_node_fat32(struct vfs *vfs,
                                                const struct vfs_node *node) {
    uint32_t slot;

    if (vfs == 0 || node == 0 || node->mount_kind != VFS_MOUNT_FAT32) {
        return 0;
    }
    if (node->mount_slot == 0u) {
        return &vfs->fat32;
    }
    slot = node->mount_slot - 1u;
    if (slot >= VFS_MOUNT_SLOT_MAX ||
        !vfs->mounts[slot].used ||
        vfs->mounts[slot].kind != VFS_MOUNT_FAT32) {
        return 0;
    }
    return &vfs->mounts[slot].fat32;
}

static struct nxfs_volume *i386_vfs_resolve_nxfs(struct vfs *vfs,
                                                 const char *path,
                                                 const char **relative_out,
                                                 uint32_t *mount_slot_out) {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t i = 0u;
    uint32_t slot;
    const char *relative;

    if (vfs == 0 || path == 0 || relative_out == 0 || mount_slot_out == 0) {
        return 0;
    }
    while (*path == '/') {
        path++;
    }
    while (path[i] != '\0' && path[i] != '/') {
        if (i + 1u >= sizeof(name)) {
            return 0;
        }
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    if (vfs_find_dynamic_mount(vfs, name, &slot) &&
        vfs->mounts[slot].kind == VFS_MOUNT_NXFS) {
        relative = path + i;
        while (*relative == '/') {
            relative++;
        }
        *relative_out = relative;
        *mount_slot_out = slot + 1u;
        return &vfs->mounts[slot].nxfs;
    }
    return 0;
}

static struct nxfs_volume *i386_vfs_node_nxfs(struct vfs *vfs,
                                              const struct vfs_node *node) {
    uint32_t slot;

    if (vfs == 0 || node == 0 || node->mount_kind != VFS_MOUNT_NXFS) {
        return 0;
    }
    if (node->mount_slot == 0u) {
        return &vfs->nxfs;
    }
    slot = node->mount_slot - 1u;
    if (slot >= VFS_MOUNT_SLOT_MAX ||
        !vfs->mounts[slot].used ||
        vfs->mounts[slot].kind != VFS_MOUNT_NXFS) {
        return 0;
    }
    return &vfs->mounts[slot].nxfs;
}

static int64_t i386_vfs_emit_dirent(struct vfs_dirent *entry,
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

static int i386_vfs_mountpoint_hidden_from_root(const struct vfs *vfs,
                                                uint8_t kind,
                                                uint32_t mount_slot) {
    return vfs != 0 &&
           vfs->root_kind == kind &&
           vfs->root_slot == mount_slot;
}

static int64_t i386_vfs_readdir_root_mountpoint(struct vfs *vfs,
                                                uint32_t *index_io,
                                                struct vfs_dirent *entry,
                                                uint32_t mountpoint_index) {
    struct vfs_mount_info info;
    uint32_t source_known = 0u;
    uint32_t builtin_count;
    uint32_t visible_index = 0u;

    if (vfs == 0 || index_io == 0 || entry == 0) {
        return -1;
    }
    builtin_count = vfs_builtin_mount_count(vfs);
    for (uint32_t i = 0u; i < builtin_count; i++) {
        if (!vfs_get_builtin_mount(vfs, i, &info, &source_known) ||
            i386_vfs_mountpoint_hidden_from_root(vfs, info.kind, 0u)) {
            continue;
        }
        if (visible_index == mountpoint_index) {
            (void)source_known;
            return i386_vfs_emit_dirent(entry, index_io, info.name, 0u, VFS_ATTR_DIR);
        }
        visible_index++;
    }
    for (uint32_t i = 0u; i < vfs_mount_count(vfs); i++) {
        if (vfs_get_mount(vfs, i, &info) != 0 ||
            i386_vfs_mountpoint_hidden_from_root(vfs, info.kind, i + 1u)) {
            continue;
        }
        if (visible_index == mountpoint_index) {
            return i386_vfs_emit_dirent(entry, index_io, info.name, 0u, VFS_ATTR_DIR);
        }
        visible_index++;
    }
    return 0;
}

static int64_t i386_vfs_readdir_root_view_mountpoints(struct vfs *vfs,
                                                      uint32_t *index_io,
                                                      struct vfs_dirent *entry,
                                                      uint32_t native_count) {
    if (*index_io < native_count) {
        return 0;
    }
    return i386_vfs_readdir_root_mountpoint(vfs, index_io, entry, *index_io - native_count);
}

int vfs_open(struct vfs *vfs, const char *path, uint32_t flags, struct vfs_node *out) {
    struct fat32_file file;
    struct nxfs_inode inode;
    char alias_path[NOS_PATH_BUFFER_SIZE];
    const char *relative;
    const char *pseudo_child;
    struct fat32_volume *fat32;
    struct nxfs_volume *nxfs;
    uint32_t mount_slot;
    uint32_t inode_index;
    uint8_t pseudo_kind;
    int found;
    int created = 0;

    if (vfs == 0 || out == 0 ||
        (flags & ~(VFS_OPEN_CREATE |
                   VFS_OPEN_TRUNCATE |
                   VFS_OPEN_APPEND)) != 0u ||
        vfs->root_kind != VFS_MOUNT_FAT32) {
        return -1;
    }
    if (i386_vfs_pseudo_path(path, &pseudo_kind, &pseudo_child)) {
        return i386_vfs_open_pseudo(pseudo_kind, pseudo_child, flags, out);
    }
    if ((flags & (VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE | VFS_OPEN_APPEND)) == 0u &&
        i386_vfs_cmd_alias_path(path, alias_path, sizeof(alias_path))) {
        path = alias_path;
    }
    fat32 = i386_vfs_resolve_fat32(vfs, path, &relative, &mount_slot);
    nxfs = i386_vfs_resolve_nxfs(vfs, path, &relative, &mount_slot);
    if (nxfs != 0) {
        if (relative == 0 || relative[0] == '\0') {
            return -1;
        }
        found = nxfs_lookup_path(nxfs, relative, &inode_index, &inode) == 0;
        if (!found) {
            if ((flags & VFS_OPEN_CREATE) == 0u ||
                nxfs_create_path(nxfs, relative, &inode_index, &inode) != 0) {
                return -1;
            }
            created = 1;
        }
        if (inode.type != NXFS_TYPE_FILE) {
            return -1;
        }
        if ((flags & VFS_OPEN_TRUNCATE) != 0u &&
            nxfs_truncate_inode(nxfs, inode_index, &inode) != 0) {
            return -1;
        }
        vfs_set_nxfs_file_node(out, mount_slot, inode_index, &inode);
        if (created) {
            vfs_event_file_change_emit("create", relative, VFS_MOUNT_NXFS, mount_slot, inode_index, 0u);
        } else if ((flags & VFS_OPEN_TRUNCATE) != 0u) {
            vfs_event_file_change_emit("truncate", relative, VFS_MOUNT_NXFS, mount_slot, inode_index, 0u);
        }
        return 0;
    }
    if (fat32 == 0 || relative == 0 || relative[0] == '\0') {
        return -1;
    }
    found = fat32_find_path(fat32, relative, &file) == 0;
    if (!found) {
        if ((flags & VFS_OPEN_CREATE) == 0u ||
            fat32_create_path(fat32, relative, &file) != 0) {
            return -1;
        }
        created = 1;
    }
    if ((file.attributes & VFS_ATTR_DIR) != 0u) {
        return -1;
    }
    if ((flags & VFS_OPEN_TRUNCATE) != 0u &&
        fat32_truncate_file(fat32, &file) != 0) {
        return -1;
    }
    vfs_set_fat32_file_node(out, mount_slot, &file);
    if (created) {
        vfs_event_file_change_emit("create",
                                   relative,
                                   VFS_MOUNT_FAT32,
                                   mount_slot,
                                   file.first_cluster,
                                   0u);
    } else if ((flags & VFS_OPEN_TRUNCATE) != 0u) {
        vfs_event_file_change_emit("truncate",
                                   relative,
                                   VFS_MOUNT_FAT32,
                                   mount_slot,
                                   file.first_cluster,
                                   0u);
    }
    return 0;
}

int vfs_opendir(struct vfs *vfs, const char *path, struct vfs_node *out) {
    struct fat32_file directory;
    struct nxfs_inode inode;
    char alias_path[NOS_PATH_BUFFER_SIZE];
    const char *relative;
    const char *pseudo_child;
    struct fat32_volume *fat32;
    struct nxfs_volume *nxfs;
    uint32_t mount_slot;
    uint32_t inode_index;
    uint8_t pseudo_kind;

    if (vfs == 0 || out == 0 ||
        vfs->root_kind != VFS_MOUNT_FAT32) {
        return -1;
    }
    if (i386_vfs_pseudo_path(path, &pseudo_kind, &pseudo_child)) {
        return i386_vfs_opendir_pseudo(pseudo_kind, pseudo_child, out);
    }
    if (i386_vfs_cmd_alias_path(path, alias_path, sizeof(alias_path))) {
        path = alias_path;
    }
    fat32 = i386_vfs_resolve_fat32(vfs, path, &relative, &mount_slot);
    nxfs = i386_vfs_resolve_nxfs(vfs, path, &relative, &mount_slot);
    if (nxfs != 0) {
        if (relative == 0 || relative[0] == '\0') {
            if (nxfs_read_inode(nxfs, 0u, &inode) != 0 ||
                inode.type != NXFS_TYPE_DIR) {
                return -1;
            }
            vfs_set_nxfs_file_node(out, mount_slot, 0u, &inode);
            return 0;
        }
        if (nxfs_lookup_path(nxfs, relative, &inode_index, &inode) != 0 ||
            inode.type != NXFS_TYPE_DIR) {
            return -1;
        }
        vfs_set_nxfs_file_node(out, mount_slot, inode_index, &inode);
        return 0;
    }
    if (fat32 == 0 || relative == 0) {
        return -1;
    }
    if (relative[0] == '\0') {
        fat32_get_root_dir(fat32, &directory);
    } else if (fat32_find_path(fat32, relative, &directory) != 0 ||
               (directory.attributes & VFS_ATTR_DIR) == 0u) {
        return -1;
    }
    vfs_set_fat32_dir_node(out, mount_slot, &directory);
    if (relative[0] == '\0' &&
        vfs->root_kind == VFS_MOUNT_FAT32 &&
        vfs->root_slot == mount_slot) {
        out->aux_data = VFS_NODE_FLAG_ROOT_VIEW;
    }
    return 0;
}

int vfs_mkdir(struct vfs *vfs, const char *path) {
    const char *relative;
    struct fat32_volume *fat32;
    struct nxfs_volume *nxfs;
    uint32_t mount_slot;
    uint8_t pseudo_kind;

    if (vfs == 0 || path == 0 || path[0] == '\0' ||
        i386_vfs_pseudo_path(path, &pseudo_kind, &relative)) {
        return -1;
    }
    nxfs = i386_vfs_resolve_nxfs(vfs, path, &relative, &mount_slot);
    if (nxfs != 0) {
        if (relative == 0 || relative[0] == '\0' ||
            nxfs_mkdir_path(nxfs, relative, 0, 0) != 0 ||
            blockdev_flush(nxfs->bdev) != 0) {
            return -1;
        }
        vfs_event_file_change_emit("mkdir", relative, VFS_MOUNT_NXFS, mount_slot, 0u, 0u);
        return 0;
    }
    fat32 = i386_vfs_resolve_fat32(vfs, path, &relative, &mount_slot);
    if (fat32 == 0 || relative == 0 || relative[0] == '\0' ||
        fat32_mkdir_path(fat32, relative, 0) != 0 ||
        blockdev_flush(fat32->bdev) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("mkdir", relative, VFS_MOUNT_FAT32, mount_slot, 0u, 0u);
    return 0;
}

int vfs_rmdir(struct vfs *vfs, const char *path) {
    const char *relative;
    struct fat32_volume *fat32;
    struct nxfs_volume *nxfs;
    uint32_t mount_slot;
    uint8_t pseudo_kind;

    if (vfs == 0 || path == 0 || path[0] == '\0' ||
        i386_vfs_pseudo_path(path, &pseudo_kind, &relative)) {
        return -1;
    }
    nxfs = i386_vfs_resolve_nxfs(vfs, path, &relative, &mount_slot);
    if (nxfs != 0) {
        if (relative == 0 || relative[0] == '\0' ||
            nxfs_rmdir_path(nxfs, relative) != 0 ||
            blockdev_flush(nxfs->bdev) != 0) {
            return -1;
        }
        vfs_event_file_change_emit("rmdir", relative, VFS_MOUNT_NXFS, mount_slot, 0u, 0u);
        return 0;
    }
    fat32 = i386_vfs_resolve_fat32(vfs, path, &relative, &mount_slot);
    if (fat32 == 0 || relative == 0 || relative[0] == '\0' ||
        fat32_rmdir_path(fat32, relative) != 0 ||
        blockdev_flush(fat32->bdev) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("rmdir", relative, VFS_MOUNT_FAT32, mount_slot, 0u, 0u);
    return 0;
}

int vfs_unlink(struct vfs *vfs, const char *path) {
    const char *relative;
    struct fat32_volume *fat32;
    struct nxfs_volume *nxfs;
    uint32_t mount_slot;
    uint8_t pseudo_kind;

    if (vfs == 0 || path == 0 || path[0] == '\0' ||
        i386_vfs_pseudo_path(path, &pseudo_kind, &relative)) {
        return -1;
    }
    nxfs = i386_vfs_resolve_nxfs(vfs, path, &relative, &mount_slot);
    if (nxfs != 0) {
        if (relative == 0 || relative[0] == '\0' ||
            nxfs_unlink_path(nxfs, relative) != 0 ||
            blockdev_flush(nxfs->bdev) != 0) {
            return -1;
        }
        vfs_event_file_change_emit("unlink", relative, VFS_MOUNT_NXFS, mount_slot, 0u, 0u);
        return 0;
    }
    fat32 = i386_vfs_resolve_fat32(vfs, path, &relative, &mount_slot);
    if (fat32 == 0 || relative == 0 || relative[0] == '\0' ||
        fat32_unlink_path(fat32, relative) != 0 ||
        blockdev_flush(fat32->bdev) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("unlink", relative, VFS_MOUNT_FAT32, mount_slot, 0u, 0u);
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
        node->kind != VFS_NODE_FILE) {
        return -1;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_read_from_devfs(vfs, node, offset_io, buffer, size, flags);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_read_from_procfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_read_from_eventfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume *nxfs = i386_vfs_node_nxfs(vfs, node);

        if (nxfs == 0 ||
            nxfs_read_file_range(nxfs,
                                 &node->handle.nxfs_inode,
                                 *offset_io,
                                 buffer,
                                 size,
                                 &bytes_read) != 0) {
            return -1;
        }
        *offset_io += bytes_read;
        return bytes_read;
    }
    if (node->mount_kind != VFS_MOUNT_FAT32 ||
        i386_vfs_node_fat32(vfs, node) == 0 ||
        fat32_read_file_range(i386_vfs_node_fat32(vfs, node),
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

    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0 ||
        node->kind != VFS_NODE_FILE) {
        return -1;
    }
    if (node->mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_write_to_devfs(vfs, node, offset_io, buffer, size);
    }
    if (node->mount_kind == VFS_MOUNT_PROCFS ||
        node->mount_kind == VFS_MOUNT_EVENTFS) {
        return -1;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume *nxfs = i386_vfs_node_nxfs(vfs, node);

        if (nxfs == 0 ||
            nxfs_write_file_range(nxfs,
                                  node->aux_index,
                                  &node->handle.nxfs_inode,
                                  *offset_io,
                                  buffer,
                                  size,
                                  &bytes_written) != 0 ||
            blockdev_flush(nxfs->bdev) != 0) {
            return -1;
        }
        *offset_io += bytes_written;
        vfs_event_file_change_emit("write",
                                   opened_path,
                                   VFS_MOUNT_NXFS,
                                   node->mount_slot,
                                   node->aux_index,
                                   bytes_written);
        return (int64_t)bytes_written;
    }
    if (node->mount_kind != VFS_MOUNT_FAT32 ||
        i386_vfs_node_fat32(vfs, node) == 0 ||
        fat32_write_file_range(i386_vfs_node_fat32(vfs, node),
                               &node->handle.fat32_file,
                               *offset_io,
                               buffer,
                               size,
                               &bytes_written) != 0 ||
        blockdev_flush(i386_vfs_node_fat32(vfs, node)->bdev) != 0) {
        return -1;
    }
    *offset_io += bytes_written;
    vfs_event_file_change_emit("write",
                               opened_path,
                               VFS_MOUNT_FAT32,
                               node->mount_slot,
                               node->handle.fat32_file.first_cluster,
                               bytes_written);
    return (int64_t)bytes_written;
}

int64_t vfs_readdir(struct vfs *vfs,
                    struct vfs_node *node,
                    uint32_t *index_io,
                    struct vfs_dirent *entry) {
    struct fat32_file file;
    struct fat32_volume *fat32;

    if (vfs == 0 || node == 0 || index_io == 0 || entry == 0 ||
        node->kind != VFS_NODE_DIR) {
        return -1;
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
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume *nxfs = i386_vfs_node_nxfs(vfs, node);
        struct nxfs_dir_entry nxfs_entry;
        struct nxfs_inode nxfs_inode;

        if (nxfs == 0 ||
            nxfs_get_dir_entry(nxfs,
                               node->aux_index,
                               &node->handle.nxfs_inode,
                               *index_io,
                               &nxfs_entry) != 0) {
            return 0;
        }
        vfs_copy_name(entry->name, sizeof(entry->name), nxfs_entry.name);
        entry->size = 0u;
        entry->attributes = 0u;
        if (nxfs_read_inode(nxfs, nxfs_entry.inode, &nxfs_inode) == 0) {
            entry->size = nxfs_inode.size;
            if (nxfs_inode.type == NXFS_TYPE_DIR) {
                entry->attributes = VFS_ATTR_DIR;
            }
        }
        (*index_io)++;
        return 1;
    }
    if (node->mount_kind != VFS_MOUNT_FAT32 ||
        (fat32 = i386_vfs_node_fat32(vfs, node)) == 0) {
        return -1;
    }
    if (fat32_get_dir_entry(fat32,
                            &node->handle.fat32_file,
                            *index_io,
                            &file) != 0) {
        if ((node->aux_data & VFS_NODE_FLAG_ROOT_VIEW) == 0u) {
            return 0;
        }
        uint32_t native_count = 0u;

        while (fat32_get_dir_entry(fat32,
                                   &node->handle.fat32_file,
                                   native_count,
                                   &file) == 0) {
            native_count++;
        }
        return i386_vfs_readdir_root_view_mountpoints(vfs, index_io, entry, native_count);
    }

    return i386_vfs_emit_dirent(entry, index_io, file.name, file.size, file.attributes);
}
