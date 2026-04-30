#include "fs/vfs_internal.h"
#include "kernel/public/sys/syscall.h"
#include "lib/string.h"

static int vfs_is_root_path(const char *path) {
    if (path == 0) {
        return 0;
    }
    return path[0] == '\0' ||
           streq(path, "/");
}

static int vfs_parse_root_child(const char *path, char *name, uint32_t name_size, char *child, uint32_t child_size) {
    uint32_t i = 0;
    uint32_t j = 0;

    if (path == 0 || path[0] != '/' || name == 0 || name_size == 0 || child == 0 || child_size == 0) {
        return 0;
    }
    path++;
    if (*path == '\0') {
        return 0;
    }
    while (path[i] != '\0' && path[i] != '/' && i + 1u < name_size) {
        name[i] = path[i];
        i++;
    }
    if (path[i] != '\0' && path[i] != '/' && i + 1u >= name_size) {
        return 0;
    }
    name[i] = '\0';
    if (path[i] == '\0') {
        child[0] = '\0';
        return 1;
    }
    i++;
    while (path[i + j] != '\0' && j + 1u < child_size) {
        child[j] = path[i + j];
        j++;
    }
    if (path[i + j] != '\0') {
        return 0;
    }
    child[j] = '\0';
    return 1;
}

static void vfs_path_reset(struct vfs_path *path) {
    if (path == 0) {
        return;
    }
    path->root_dir = 0;
    path->has_child = 0;
    path->child_is_root = 0;
    path->mount_kind = VFS_MOUNT_NONE;
    path->mount_slot = 0;
    path->child[0] = '\0';
}

static int vfs_path_use_root_mount(const struct vfs *vfs, struct vfs_path *out) {
    if (!vfs_has_root_override(vfs)) {
        return 0;
    }
    out->mount_kind = vfs->root_kind;
    out->mount_slot = vfs->root_slot;
    return 1;
}

static int vfs_path_is_targetable_child(const struct vfs_path *parsed) {
    return parsed != 0 && !parsed->root_dir && !parsed->child_is_root && parsed->has_child;
}

int vfs_open_fat32(struct vfs *vfs, const struct vfs_path *parsed, uint32_t flags, struct vfs_node *out) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    struct fat32_file fat32_file;
    int created = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_find_path(fat32, parsed->child, &fat32_file) != 0) {
        int create_rc = -1;

        if ((flags & SYS_OPEN_CREAT) == 0) {
            fat32_debug_lookup_path(fat32, parsed->child);
            return -1;
        }
        create_rc = fat32_create_path(fat32, parsed->child, &fat32_file);
        if (create_rc != 0) {
            return -1;
        }
        created = 1;
    }
    if ((fat32_file.attributes & VFS_ATTR_DIR) != 0) {
        return -1;
    }
    vfs_set_fat32_file_node(out, parsed->mount_slot, &fat32_file);
    if (created) {
        vfs_event_file_change_emit("create",
                                   parsed->child,
                                   VFS_MOUNT_FAT32,
                                   parsed->mount_slot,
                                   vfs_node_native_id(out),
                                   0);
    }
    return 0;
}

int vfs_open_nxfs(struct vfs *vfs, const struct vfs_path *parsed, uint32_t flags, struct vfs_node *out) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    struct nxfs_inode nxfs_inode;
    uint32_t inode_index = 0;
    int created = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_lookup_root(nxfs, parsed->child, &inode_index, &nxfs_inode) != 0) {
        if ((flags & SYS_OPEN_CREAT) == 0 ||
            nxfs_create_path(nxfs, parsed->child, &inode_index, &nxfs_inode) != 0) {
            return -1;
        }
        created = 1;
    }
    if (nxfs_inode.type != NXFS_TYPE_FILE) {
        return -1;
    }
    vfs_set_nxfs_file_node(out, parsed->mount_slot, inode_index, &nxfs_inode);
    if (created) {
        vfs_event_file_change_emit("create",
                                   parsed->child,
                                   VFS_MOUNT_NXFS,
                                   parsed->mount_slot,
                                   vfs_node_native_id(out),
                                   0);
    }
    return 0;
}

int vfs_opendir_mount_root(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out) {
    if (parsed->mount_kind == VFS_MOUNT_FAT32) {
        struct vfs_mount_instance mount;
        struct fat32_file root_dir;
        struct fat32_volume *fat32;

        if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
            return -1;
        }
        fat32 = (struct fat32_volume *)mount.fs_data;
        fat32_get_root_dir(fat32, &root_dir);
        vfs_set_fat32_dir_node(out, parsed->mount_slot, &root_dir);
    } else if (parsed->mount_kind == VFS_MOUNT_NXFS) {
        struct vfs_mount_instance mount;
        struct nxfs_inode root_dir;
        struct nxfs_volume *nxfs;

        if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
            return -1;
        }
        nxfs = (struct nxfs_volume *)mount.fs_data;
        if (nxfs_read_inode(nxfs, 0, &root_dir) != 0 || root_dir.type != NXFS_TYPE_DIR) {
            return -1;
        }
        vfs_set_nxfs_file_node(out, parsed->mount_slot, 0, &root_dir);
    } else {
        vfs_set_dir_node(out, parsed->mount_kind);
    }

    if (vfs_has_root_override(vfs) &&
        parsed->mount_kind == vfs->root_kind &&
        parsed->mount_slot == vfs->root_slot) {
        out->aux_data = VFS_NODE_FLAG_ROOT_VIEW;
    }
    out->mount_slot = parsed->mount_slot;
    return 0;
}

int vfs_opendir_fat32(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;
    struct fat32_file dir_entry;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_find_path(fat32, parsed->child, &dir_entry) != 0 ||
        (dir_entry.attributes & VFS_ATTR_DIR) == 0) {
        return -1;
    }
    vfs_set_fat32_dir_node(out, parsed->mount_slot, &dir_entry);
    return 0;
}

int vfs_opendir_nxfs(struct vfs *vfs, const struct vfs_path *parsed, struct vfs_node *out) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    struct nxfs_inode nxfs_inode;
    uint32_t inode_index = 0;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_lookup_path(nxfs, parsed->child, &inode_index, &nxfs_inode) != 0 ||
        nxfs_inode.type != NXFS_TYPE_DIR) {
        return -1;
    }
    vfs_set_nxfs_file_node(out, parsed->mount_slot, inode_index, &nxfs_inode);
    return 0;
}

int vfs_mkdir_fat32(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_mkdir_path(fat32, parsed->child, 0) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("mkdir", parsed->child, VFS_MOUNT_FAT32, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_mkdir_nxfs(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_mkdir_path(nxfs, parsed->child, 0, 0) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("mkdir", parsed->child, VFS_MOUNT_NXFS, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_rmdir_fat32(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_rmdir_path(fat32, parsed->child) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("rmdir", parsed->child, VFS_MOUNT_FAT32, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_rmdir_nxfs(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_rmdir_path(nxfs, parsed->child) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("rmdir", parsed->child, VFS_MOUNT_NXFS, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_unlink_fat32(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct fat32_volume *fat32;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_FAT32, parsed->mount_slot, &mount)) {
        return -1;
    }
    fat32 = (struct fat32_volume *)mount.fs_data;
    if (fat32_unlink_path(fat32, parsed->child) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("unlink", parsed->child, VFS_MOUNT_FAT32, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_unlink_nxfs(struct vfs *vfs, const struct vfs_path *parsed) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (!vfs_get_mount_instance(vfs, VFS_MOUNT_NXFS, parsed->mount_slot, &mount)) {
        return -1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    if (nxfs_unlink_path(nxfs, parsed->child) != 0) {
        return -1;
    }
    vfs_event_file_change_emit("unlink", parsed->child, VFS_MOUNT_NXFS, parsed->mount_slot, 0, 0);
    return 0;
}

int vfs_contains_char(const char *text, char needle) {
    if (text == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (*text == needle) {
            return 1;
        }
        text++;
    }
    return 0;
}

int vfs_parse_path_for_vfs(const struct vfs *vfs, const char *path, struct vfs_path *out) {
    char mount_name[NOS_NAME_BUFFER_SIZE];

    if (out == 0) {
        return 0;
    }
    vfs_path_reset(out);

    if (vfs_is_root_path(path)) {
        if (vfs_path_use_root_mount(vfs, out)) {
            out->child_is_root = 1;
            return 1;
        }
        out->root_dir = 1;
        out->mount_kind = VFS_MOUNT_ROOT;
        return 1;
    }
    if (path == 0) {
        return 0;
    }
    if (path[0] != '/') {
        vfs_copy_name(out->child, sizeof(out->child), path);
        if (!vfs_path_use_root_mount(vfs, out)) {
            out->mount_kind = VFS_MOUNT_FAT32;
        }
        out->has_child = 1;
        return 1;
    }
    if (!vfs_parse_root_child(path, mount_name, sizeof(mount_name), out->child, sizeof(out->child))) {
        return 0;
    }
    if (streq(mount_name, "fat")) {
        out->mount_kind = VFS_MOUNT_FAT32;
    } else if (streq(mount_name, "nxfs")) {
        out->mount_kind = VFS_MOUNT_NXFS;
    } else if (streq(mount_name, "dev")) {
        out->mount_kind = VFS_MOUNT_DEVFS;
    } else if (streq(mount_name, "proc")) {
        out->mount_kind = VFS_MOUNT_PROCFS;
    } else if (streq(mount_name, "event")) {
        out->mount_kind = VFS_MOUNT_EVENTFS;
    } else {
        uint32_t slot;

        if (vfs_find_dynamic_mount(vfs, mount_name, &slot)) {
            out->mount_kind = vfs->mounts[slot].kind;
            out->mount_slot = slot + 1u;
        } else {
            /* Treat unknown absolute paths as root-relative paths before failing mount lookup. */
            vfs_copy_name(out->child, sizeof(out->child), path + 1u);
            if (!vfs_path_use_root_mount(vfs, out)) {
                out->mount_kind = VFS_MOUNT_FAT32;
            }
            out->has_child = 1;
            return 1;
        }
    }
    if (out->child[0] == '\0') {
        out->child_is_root = 1;
        return 1;
    }
    out->has_child = 1;
    return 1;
}

int vfs_resolve_mount_target_name(const struct vfs *vfs, const char *target, char *name, uint32_t name_size) {
    char child[2];

    (void)vfs;

    if (target == 0 || name == 0 || name_size == 0 || target[0] != '/') {
        return 0;
    }
    return vfs_parse_root_child(target, name, name_size, child, sizeof(child)) && child[0] == '\0';
}

int vfs_open(struct vfs *vfs, const char *path, uint32_t flags, struct vfs_node *out) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (out == 0 || !vfs_parse_path_for_vfs(vfs, path, &parsed) || !vfs_path_is_targetable_child(&parsed)) {
        return -1;
    }
    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }

    if (parsed.mount_kind == VFS_MOUNT_DEVFS) {
        return vfs_devfs_lookup(parsed.child, out);
    }
    if (parsed.mount_kind == VFS_MOUNT_PROCFS) {
        if ((flags & SYS_OPEN_CREAT) != 0) {
            return -1;
        }
        return vfs_procfs_lookup(parsed.child, out);
    }
    if (parsed.mount_kind == VFS_MOUNT_EVENTFS) {
        if ((flags & SYS_OPEN_CREAT) != 0) {
            return -1;
        }
        return vfs_eventfs_lookup(parsed.child, out);
    }
    ops = vfs_mount_ops(parsed.mount_kind);
    return ops != 0 && ops->open_file != 0 ? ops->open_file(vfs, &parsed, flags, out) : -1;
}

int vfs_opendir_root_mount(struct vfs *vfs, struct vfs_node *out) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (vfs == 0 || out == 0) {
        return -1;
    }

    vfs_path_reset(&parsed);
    if (vfs_has_root_override(vfs)) {
        parsed.mount_kind = vfs->root_kind;
        parsed.mount_slot = vfs->root_slot;
    } else {
        parsed.mount_kind = VFS_MOUNT_FAT32;
        parsed.mount_slot = 0;
    }
    parsed.child_is_root = 1;

    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }

    ops = vfs_mount_ops(parsed.mount_kind);
    return ops != 0 && ops->open_mount_root != 0 ? ops->open_mount_root(vfs, &parsed, out) : -1;
}

int vfs_opendir(struct vfs *vfs, const char *path, struct vfs_node *out) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (out == 0 || !vfs_parse_path_for_vfs(vfs, path, &parsed)) {
        return -1;
    }
    if (parsed.root_dir) {
        vfs_set_dir_node(out, VFS_MOUNT_ROOT);
        return 0;
    }
    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }
    ops = vfs_mount_ops(parsed.mount_kind);
    if (parsed.child_is_root && parsed.mount_kind == VFS_MOUNT_DEVFS) {
        vfs_set_devfs_node(out, VFS_NODE_DIR, 0);
        return 0;
    }
    if (parsed.child_is_root && parsed.mount_kind == VFS_MOUNT_PROCFS) {
        vfs_set_procfs_node(out, VFS_NODE_DIR, VFS_PROC_ROOT, 0);
        return 0;
    }
    if (parsed.child_is_root && parsed.mount_kind == VFS_MOUNT_EVENTFS) {
        vfs_set_eventfs_node(out, VFS_NODE_DIR, VFS_EVENT_ROOT);
        return 0;
    }
    if (parsed.child_is_root) {
        return ops != 0 && ops->open_mount_root != 0 ? ops->open_mount_root(vfs, &parsed, out) : -1;
    }
    if (!parsed.has_child) {
        return -1;
    }
    if (parsed.mount_kind == VFS_MOUNT_DEVFS) {
        return -1;
    }
    if (parsed.mount_kind == VFS_MOUNT_PROCFS) {
        return vfs_procfs_opendir(parsed.child, out);
    }
    if (parsed.mount_kind == VFS_MOUNT_EVENTFS) {
        return vfs_eventfs_opendir(parsed.child, out);
    }
    return ops != 0 && ops->open_dir != 0 ? ops->open_dir(vfs, &parsed, out) : -1;
}

int vfs_mkdir(struct vfs *vfs, const char *path) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (vfs == 0 || !vfs_parse_path_for_vfs(vfs, path, &parsed) ||
        !vfs_path_is_targetable_child(&parsed)) {
        return -1;
    }
    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }
    ops = vfs_mount_ops(parsed.mount_kind);
    return ops != 0 && ops->mkdir_path != 0 ? ops->mkdir_path(vfs, &parsed) : -1;
}

int vfs_rmdir(struct vfs *vfs, const char *path) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (vfs == 0 || !vfs_parse_path_for_vfs(vfs, path, &parsed) ||
        !vfs_path_is_targetable_child(&parsed)) {
        return -1;
    }
    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }
    ops = vfs_mount_ops(parsed.mount_kind);
    return ops != 0 && ops->rmdir_path != 0 ? ops->rmdir_path(vfs, &parsed) : -1;
}

int vfs_unlink(struct vfs *vfs, const char *path) {
    struct vfs_path parsed;
    const struct vfs_mount_ops *ops;

    if (vfs == 0 || !vfs_parse_path_for_vfs(vfs, path, &parsed) ||
        !vfs_path_is_targetable_child(&parsed)) {
        return -1;
    }
    if (!vfs_mount_ready(vfs, parsed.mount_kind)) {
        return -1;
    }
    ops = vfs_mount_ops(parsed.mount_kind);
    return ops != 0 && ops->unlink_path != 0 ? ops->unlink_path(vfs, &parsed) : -1;
}
