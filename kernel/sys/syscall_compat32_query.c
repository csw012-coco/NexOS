#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/rtc/cmos.h"
#include "fs/vfs.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/profile.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/proc/process.h"
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

static void compat32_fill_pci_info(uint32_t index, struct syscall_pci_info *info) {
    struct pci_device_info device;

    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    if (!pci_find_device_by_index(index, &device)) {
        return;
    }
    info->present = 1u;
    info->bus = device.bus;
    info->slot = device.slot;
    info->function = device.function;
    info->class_code = device.class_code;
    info->subclass = device.subclass;
    info->prog_if = device.prog_if;
    info->irq_line = device.irq_line;
    info->irq_pin = device.irq_pin;
    info->vendor_id = device.vendor_id;
    info->device_id = device.device_id;
    info->bar0 = device.bar0;
    info->bar1 = device.bar1;
    info->bar2 = device.bar2;
    info->bar3 = device.bar3;
    info->bar4 = device.bar4;
    info->bar5 = device.bar5;
}

static void compat32_fill_rtc_info(struct syscall_rtc_info *info) {
    struct cmos_rtc_info rtc;

    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    (void)cmos_rtc_query(&rtc);
    info->present = rtc.present;
    info->updating = rtc.updating;
    info->valid = rtc.valid;
    info->binary_mode = rtc.binary_mode;
    info->hour_24 = rtc.hour_24;
    info->status_a = rtc.status_a;
    info->status_b = rtc.status_b;
    info->century = rtc.century;
    info->raw_year = rtc.raw_year;
    info->second = rtc.second;
    info->minute = rtc.minute;
    info->hour = rtc.hour;
    info->weekday = rtc.weekday;
    info->day = rtc.day;
    info->month = rtc.month;
    info->year = rtc.year;
    info->unix_time = rtc.unix_time;
}

uint32_t syscall_compat32_query(struct syscall_compat32_context *ctx,
                                 uint32_t kind,
                                 uint32_t arg0,
                                 uint32_t arg1,
                                 uint32_t user_info) {
    if (ctx == 0) {
        return 0u;
    }
    if (kind == SYS_QUERY_TTY) {
        struct syscall_tty_info info = {0};
        static const char tty_path[] = "/dev/tty0";
        uint32_t fd_kind = ctx->fd_kind != 0 ? ctx->fd_kind(arg0) : 0u;

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
            return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
        }
        return 0u;
    }
    if (kind == SYS_QUERY_FD) {
        struct syscall_fd_info info;
        int found = ctx->fd_query != 0 ? ctx->fd_query(arg0, &info) : 0;

        if (found <= 0) {
            return (uint32_t)found;
        }
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_BLOCK) {
        struct syscall_block_info info = {0};
        struct block_device *dev = blockdev_get(arg0);

        if (dev == 0 || ctx->fill_block_info == 0) {
            return 0u;
        }
        ctx->fill_block_info(&info, arg0, dev);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_PART) {
        struct syscall_partition_info info = {0};
        struct block_device *dev = blockdev_get(arg0);
        struct blockdev_partition part;

        if (dev == 0 || ctx->fill_part_info == 0 ||
            blockdev_partition_get(dev, arg1, &part) != 0) {
            return 0u;
        }
        ctx->fill_part_info(&info, arg0, arg1, &part);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_MOUNT) {
        struct syscall_mount_info info;
        int found = ctx->fill_mount_info != 0
            ? ctx->fill_mount_info(&info, arg0)
            : 0;

        if (found <= 0) {
            return (uint32_t)found;
        }
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_MACHINE_INFO) {
        struct syscall_machine_info info;

        if (ctx->fill_machine_info == 0) {
            return 0u;
        }
        ctx->fill_machine_info(&info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_BOOT_INFO) {
        return ctx->boot_info != 0 &&
               arch_copy_to_user(user_info, ctx->boot_info, sizeof(*ctx->boot_info))
            ? 1u
            : 0u;
    }
    if (kind == SYS_QUERY_FB) {
        return ctx->fb_info != 0 &&
               arch_copy_to_user(user_info, ctx->fb_info, sizeof(*ctx->fb_info))
            ? 1u
            : 0u;
    }
    if (kind == SYS_QUERY_MEMMAP) {
        struct syscall_memmap_info info;

        if (ctx->memmap == 0 || arg0 >= ctx->memmap_count) {
            return 0u;
        }
        info.base = ctx->memmap[arg0].base;
        info.length = ctx->memmap[arg0].length;
        info.type = ctx->memmap[arg0].type;
        info.reserved = ctx->memmap[arg0].reserved;
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_PMM) {
        struct syscall_pmm_info info;

        if (ctx->pmm_total_pages == 0 ||
            ctx->pmm_free_pages == 0 ||
            ctx->pmm_reserved_pages == 0) {
            return 0u;
        }
        info.total_pages = ctx->pmm_total_pages();
        info.free_pages = ctx->pmm_free_pages();
        info.used_pages = ctx->pmm_reserved_pages();
        info.dropped_pages = 0u;
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_VM) {
        struct syscall_vm_info info;

        syscall_compat32_vm_snapshot(ctx, &info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_PROGRAM) {
        struct syscall_program_info info;
        const char *name = process_program_name(arg0);

        if (name == 0) {
            return 0u;
        }
        compat32_copy_name(info.name, sizeof(info.name), name);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
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
            return arch_copy_to_user(user_info, &fat_info, sizeof(fat_info)) ? 1u : 0u;
        }
        return arch_copy_to_user(user_info, &root_info, sizeof(root_info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_ROOT_FIND || kind == SYS_QUERY_FAT_ROOT_FIND) {
        char name[NOS_NAME_BUFFER_SIZE];
        struct syscall_root_entry_info root_info;

        if (!arch_copy_user_cstr(name, arg0, sizeof(name)) ||
            !compat32_root_find(ctx->vfs, name, &root_info)) {
            return 0u;
        }
        if (kind == SYS_QUERY_FAT_ROOT_FIND) {
            struct syscall_fat_entry_info fat_info;

            compat32_copy_name(fat_info.name, sizeof(fat_info.name), root_info.name);
            fat_info.first_cluster = root_info.native_id;
            fat_info.size = root_info.size;
            fat_info.attributes = root_info.attributes;
            return arch_copy_to_user(user_info, &fat_info, sizeof(fat_info)) ? 1u : 0u;
        }
        return arch_copy_to_user(user_info, &root_info, sizeof(root_info)) ? 1u : 0u;
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
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_PCI) {
        struct syscall_pci_info info;

        compat32_fill_pci_info(arg0, &info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_RTC) {
        struct syscall_rtc_info info;

        compat32_fill_rtc_info(&info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_PROFILE) {
        struct syscall_profile_info info;

        if ((arg1 & SYS_PROFILE_QUERY_RESET) != 0u) {
            kernel_profile_reset();
        }
        if (!kernel_profile_query(arg0, &info)) {
            return 0u;
        }
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_AC97) {
        struct syscall_ac97_info info = {0};

        kernel_query_ac97_info(&info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_HDA) {
        struct syscall_hda_info info = {0};

        kernel_query_hda_info(&info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_RTL8139) {
        struct syscall_rtl8139_info info = {0};

        kernel_query_rtl8139_info(&info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    if (kind == SYS_QUERY_AUDIO) {
        struct syscall_audio_info info = {0};

        (void)kernel_query_audio_info(arg0, &info);
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
    }
    return 0u;
}
