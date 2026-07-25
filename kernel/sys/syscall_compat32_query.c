#include "fs/vfs.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "lib/string.h"

static void compat32_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int compat32_root_entry_from_dirent(struct vfs *vfs,
                                           const struct vfs_dirent *dirent,
                                           struct syscall_root_entry_info *info) {
    struct vfs_node node;
    char path[NOS_NAME_BUFFER_SIZE + 2u];

    if (vfs == 0 || dirent == 0 || info == 0) {
        return 0;
    }
    compat32_copy_name(info->name, sizeof(info->name), dirent->name);
    info->native_id = 0u;
    info->size = dirent->size;
    info->attributes = dirent->attributes;
    path[0] = '/';
    compat32_copy_name(&path[1], sizeof(path) - 1u, dirent->name);
    if (vfs_open(vfs, path, 0u, &node) == 0 ||
        vfs_opendir(vfs, path, &node) == 0) {
        info->native_id = vfs_node_native_id(&node);
    }
    return 1;
}

static int compat32_root_query(struct vfs *vfs,
                               uint32_t index,
                               struct syscall_root_entry_info *info) {
    struct vfs_node root;
    struct vfs_dirent dirent;
    uint32_t dir_index = index;

    if (vfs == 0 || info == 0 || vfs_opendir(vfs, "/", &root) != 0) {
        return 0;
    }
    if (vfs_readdir(vfs, &root, &dir_index, &dirent) != 1) {
        return 0;
    }
    return compat32_root_entry_from_dirent(vfs, &dirent, info);
}

static int compat32_root_find(struct vfs *vfs,
                              const char *name,
                              struct syscall_root_entry_info *info) {
    struct vfs_node root;
    struct vfs_dirent dirent;
    uint32_t dir_index = 0u;

    if (vfs == 0 || name == 0 || info == 0 ||
        vfs_opendir(vfs, "/", &root) != 0) {
        return 0;
    }
    while (vfs_readdir(vfs, &root, &dir_index, &dirent) == 1) {
        if (dirent.name[0] != '\0' && streq(dirent.name, name)) {
            return compat32_root_entry_from_dirent(vfs, &dirent, info);
        }
    }
    return 0;
}

struct compat32_query_ops {
    struct syscall_compat32_context *ctx;
    int (*copy_to_user)(uint64_t user_addr, const void *src, uint32_t size);
    int (*copy_user_cstr)(char *dest, uint64_t user_addr, uint32_t size);
    uint32_t (*fd_kind)(struct syscall_compat32_context *ctx, uint32_t fd);
    int32_t (*fd_query)(struct syscall_compat32_context *ctx,
                        uint32_t fd,
                        struct syscall_fd_info *info);
    int (*fill_mount_info)(struct syscall_compat32_context *ctx,
                           struct syscall_mount_info *info,
                           uint32_t index);
    void (*fill_machine_info)(struct syscall_compat32_context *ctx,
                              struct syscall_machine_info *info);
    void (*vm_snapshot)(struct syscall_compat32_context *ctx,
                        struct syscall_vm_info *info);
};

static int compat32_query_copy_to_user(uint64_t user_addr,
                                       const void *src,
                                       uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_to_user((uint32_t)user_addr, src, size);
}

static int compat32_query_copy_user_cstr(char *dest,
                                         uint64_t user_addr,
                                         uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_user_cstr(dest, (uint32_t)user_addr, size);
}

static uint32_t compat32_query_fd_kind(struct syscall_compat32_context *ctx,
                                       uint32_t fd) {
    return ctx != 0 && ctx->fd_kind != 0 ? ctx->fd_kind(fd) : 0u;
}

static int32_t compat32_query_fd_query(struct syscall_compat32_context *ctx,
                                       uint32_t fd,
                                       struct syscall_fd_info *info) {
    return ctx != 0 && ctx->fd_query != 0 ? ctx->fd_query(fd, info) : 0;
}

static int compat32_query_fill_mount_info(
    struct syscall_compat32_context *ctx,
    struct syscall_mount_info *info,
    uint32_t index) {
    return ctx != 0 && ctx->fill_mount_info != 0
        ? ctx->fill_mount_info(info, index)
        : 0;
}

static void compat32_query_fill_machine_info(
    struct syscall_compat32_context *ctx,
    struct syscall_machine_info *info) {
    if (ctx != 0 && ctx->fill_machine_info != 0) {
        ctx->fill_machine_info(info);
    }
}

static void compat32_query_vm_snapshot(struct syscall_compat32_context *ctx,
                                       struct syscall_vm_info *info) {
    syscall_compat32_vm_snapshot(ctx, info);
}

static void compat32_query_ops_init(struct compat32_query_ops *ops,
                                    struct syscall_compat32_context *ctx) {
    if (ops == 0) {
        return;
    }
    ops->ctx = ctx;
    ops->copy_to_user = compat32_query_copy_to_user;
    ops->copy_user_cstr = compat32_query_copy_user_cstr;
    ops->fd_kind = compat32_query_fd_kind;
    ops->fd_query = compat32_query_fd_query;
    ops->fill_mount_info = compat32_query_fill_mount_info;
    ops->fill_machine_info = compat32_query_fill_machine_info;
    ops->vm_snapshot = compat32_query_vm_snapshot;
}

static uint32_t compat32_query_copy_out(const struct compat32_query_ops *ops,
                                        uint32_t user_info,
                                        const void *info,
                                        uint32_t size) {
    return ops != 0 && ops->copy_to_user != 0 &&
           ops->copy_to_user(user_info, info, size) ? 1u : 0u;
}

uint32_t syscall_compat32_query(struct syscall_compat32_context *ctx,
                                 uint32_t kind,
                                 uint32_t arg0,
                                 uint32_t arg1,
                                 uint32_t user_info) {
    struct compat32_query_ops ops;

    if (ctx == 0) {
        return 0u;
    }
    compat32_query_ops_init(&ops, ctx);
    (void)arg1;
    if (kind == SYS_QUERY_TTY) {
        struct syscall_tty_info info = {0};
        static const char tty_path[] = "/dev/tty0";
        uint32_t fd_kind = ops.fd_kind != 0 ? ops.fd_kind(ctx, arg0) : 0u;

        if (fd_kind == KERNEL_FILE_TTY_STDIN ||
            fd_kind == KERNEL_FILE_TTY_STDOUT ||
            fd_kind == KERNEL_FILE_TTY_STDERR) {
            info.kind = SYS_TTY_KIND_VIRTUAL;
            info.index = 0u;
            info.active = 1u;
            for (uint32_t i = 0u; i + 1u < sizeof(info.path); i++) {
                info.path[i] = tty_path[i];
                if (tty_path[i] == '\0') {
                    break;
                }
            }
            return compat32_query_copy_out(
                &ops, user_info, &info, sizeof(info));
        }
        return 0u;
    }
    if (kind == SYS_QUERY_FD) {
        struct syscall_fd_info info;
        int found = ops.fd_query != 0 ? ops.fd_query(ctx, arg0, &info) : 0;

        if (found <= 0) {
            return (uint32_t)found;
        }
        return compat32_query_copy_out(&ops, user_info, &info, sizeof(info));
    }
    if (kind == SYS_QUERY_MOUNT) {
        struct syscall_mount_info info;
        int found = ops.fill_mount_info != 0
            ? ops.fill_mount_info(ctx, &info, arg0)
            : 0;

        if (found <= 0) {
            return (uint32_t)found;
        }
        return compat32_query_copy_out(&ops, user_info, &info, sizeof(info));
    }
    if (kind == SYS_QUERY_MACHINE_INFO) {
        struct syscall_machine_info info;

        if (ops.fill_machine_info == 0) {
            return 0u;
        }
        ops.fill_machine_info(ctx, &info);
        return compat32_query_copy_out(&ops, user_info, &info, sizeof(info));
    }
    if (kind == SYS_QUERY_VM) {
        struct syscall_vm_info info;

        if (ops.vm_snapshot == 0) {
            return 0u;
        }
        ops.vm_snapshot(ctx, &info);
        return compat32_query_copy_out(&ops, user_info, &info, sizeof(info));
    }
    if (kind == SYS_QUERY_ROOT || kind == SYS_QUERY_FAT_ROOT) {
        struct syscall_root_entry_info root_info;

        if (!compat32_root_query(ctx->vfs, arg0, &root_info)) {
            return 0u;
        }
        if (kind == SYS_QUERY_FAT_ROOT) {
            struct syscall_fat_entry_info fat_info;

            compat32_copy_name(fat_info.name, sizeof(fat_info.name), root_info.name);
            fat_info.first_cluster = root_info.native_id;
            fat_info.size = root_info.size;
            fat_info.attributes = root_info.attributes;
            return compat32_query_copy_out(
                &ops, user_info, &fat_info, sizeof(fat_info));
        }
        return compat32_query_copy_out(
            &ops, user_info, &root_info, sizeof(root_info));
    }
    if (kind == SYS_QUERY_ROOT_FIND || kind == SYS_QUERY_FAT_ROOT_FIND) {
        char name[NOS_NAME_BUFFER_SIZE];
        struct syscall_root_entry_info root_info;

        if (ops.copy_user_cstr == 0 ||
            !ops.copy_user_cstr(name, arg0, sizeof(name)) ||
            !compat32_root_find(ctx->vfs, name, &root_info)) {
            return 0u;
        }
        if (kind == SYS_QUERY_FAT_ROOT_FIND) {
            struct syscall_fat_entry_info fat_info;

            compat32_copy_name(fat_info.name, sizeof(fat_info.name), root_info.name);
            fat_info.first_cluster = root_info.native_id;
            fat_info.size = root_info.size;
            fat_info.attributes = root_info.attributes;
            return compat32_query_copy_out(
                &ops, user_info, &fat_info, sizeof(fat_info));
        }
        return compat32_query_copy_out(
            &ops, user_info, &root_info, sizeof(root_info));
    }
    if (kind == SYS_QUERY_KMSG) {
        struct syscall_kmsg_info info;
        uint32_t copied;

        info.total_size = kprint_log_size();
        info.offset = arg0;
        info.bytes_copied = 0u;
        for (uint32_t i = 0u; i < sizeof(info.data); i++) {
            info.data[i] = '\0';
        }
        copied = kprint_log_read(arg0, info.data, sizeof(info.data));
        if (copied == 0u) {
            return 0u;
        }
        info.bytes_copied = copied;
        return compat32_query_copy_out(&ops, user_info, &info, sizeof(info));
    }
    return 0u;
}
