#include "kernel/internal/fs/fs_service_root_query_internal.h"
#include "fs/vfs.h"

static void fs_service_root_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void fs_service_root_reset_entry(struct fs_service_root_entry_info *entry_out) {
    if (entry_out == 0) {
        return;
    }
    entry_out->name[0] = '\0';
    entry_out->native_id = 0;
    entry_out->size = 0;
    entry_out->attributes = 0;
}

static int fs_service_root_lookup_node(struct vfs *vfs, const char *name, struct vfs_node *node_out) {
    char path[NOS_PATH_BUFFER_SIZE];
    uint32_t len = 0;

    if (vfs == 0 || name == 0 || node_out == 0) {
        return 0;
    }
    path[len++] = '/';
    path[len] = '\0';
    while (*name != '\0') {
        if (len + 1u >= sizeof(path)) {
            return 0;
        }
        path[len++] = *name++;
        path[len] = '\0';
    }
    if (vfs_opendir(vfs, path, node_out) == 0) {
        return 1;
    }
    return vfs_open(vfs, path, 0, node_out) == 0;
}

static int fs_service_root_fill_entry(struct vfs *vfs,
                                      const struct vfs_dirent *dirent,
                                      struct fs_service_root_entry_info *entry_out) {
    struct vfs_node node;

    if (vfs == 0 || dirent == 0 || entry_out == 0) {
        return 0;
    }
    fs_service_root_reset_entry(entry_out);
    fs_service_root_copy_name(entry_out->name, sizeof(entry_out->name), dirent->name);
    entry_out->size = dirent->size;
    entry_out->attributes = dirent->attributes;
    if (fs_service_root_lookup_node(vfs, dirent->name, &node)) {
        entry_out->native_id = vfs_node_native_id(&node);
    }
    return 1;
}

int fs_service_root_get_entry(struct vfs *vfs, uint32_t index, struct fs_service_root_entry_info *entry_out) {
    struct vfs_node root_dir;
    struct vfs_dirent dirent;
    uint32_t dir_index = index;

    if (entry_out == 0 || vfs == 0 || vfs_opendir_root_mount(vfs, &root_dir) != 0) {
        return 0;
    }
    if (vfs_readdir(vfs, &root_dir, &dir_index, &dirent) != 1) {
        fs_service_root_reset_entry(entry_out);
        return 0;
    }
    return fs_service_root_fill_entry(vfs, &dirent, entry_out);
}

int fs_service_root_find_entry(struct vfs *vfs, const char *name, struct fs_service_root_entry_info *entry_out) {
    struct vfs_node root_dir;
    struct vfs_dirent dirent;
    uint32_t dir_index = 0;

    if (name == 0 || entry_out == 0 || vfs == 0 || vfs_opendir_root_mount(vfs, &root_dir) != 0) {
        return 0;
    }
    fs_service_root_reset_entry(entry_out);
    while (vfs_readdir(vfs, &root_dir, &dir_index, &dirent) == 1) {
        if (dirent.name[0] == '\0') {
            continue;
        }
        if (dirent.name[0] == name[0] && dirent.name[0] != '\0') {
            uint32_t i = 0;

            while (dirent.name[i] != '\0' && name[i] != '\0' && dirent.name[i] == name[i]) {
                i++;
            }
            if (dirent.name[i] == '\0' && name[i] == '\0') {
                return fs_service_root_fill_entry(vfs, &dirent, entry_out);
            }
        } else if (dirent.name[0] == '\0' && name[0] == '\0') {
            return fs_service_root_fill_entry(vfs, &dirent, entry_out);
        }
    }
    return 0;
}
