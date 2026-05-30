#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "block/blockdev.h"
#include "drivers/video/framebuffer.h"
#include "drivers/rtc/cmos.h"
#include "kernel/public/driver/driver.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"

static int vfs_process_get_pid(uint32_t pid, struct process_snapshot *out) {
    for (uint32_t i = 0; i < process_capacity(); i++) {
        struct process_snapshot snapshot;

        if (!process_get(i, &snapshot) || snapshot.pid != pid) {
            continue;
        }
        if (out != 0) {
            *out = snapshot;
        }
        return 1;
    }
    return 0;
}

static const char *vfs_process_state_name(uint32_t state) {
    switch (state) {
        case PROCESS_STATE_FREE:
            return "free";
        case PROCESS_STATE_READY:
            return "ready";
        case PROCESS_STATE_RUNNING:
            return "running";
        case PROCESS_STATE_SLEEPING:
            return "sleeping";
        case PROCESS_STATE_STOPPED:
            return "stopped";
        case PROCESS_STATE_EXITED:
            return "exited";
        case PROCESS_STATE_WAITING:
            return "waiting";
        default:
            return "free";
    }
}

static uint32_t vfs_format_proc_meminfo(char *text, uint32_t size) {
    uint32_t pos = 0;
    uint32_t total = pmm_total_pages();
    uint32_t free = pmm_free_pages();
    uint32_t used = pmm_used_pages();
    uint32_t dropped = pmm_dropped_pages();

    pos = vfs_append_text(text, pos, size, "MemTotalPages: ");
    pos = vfs_append_u32_text(text, pos, size, total);
    pos = vfs_append_text(text, pos, size, "\nMemFreePages: ");
    pos = vfs_append_u32_text(text, pos, size, free);
    pos = vfs_append_text(text, pos, size, "\nMemUsedPages: ");
    pos = vfs_append_u32_text(text, pos, size, used);
    pos = vfs_append_text(text, pos, size, "\nMemDroppedPages: ");
    pos = vfs_append_u32_text(text, pos, size, dropped);
    pos = vfs_append_text(text, pos, size, "\nMemTotalKB: ");
    pos = vfs_append_u32_text(text, pos, size, total * 4u);
    pos = vfs_append_text(text, pos, size, "\nMemFreeKB: ");
    pos = vfs_append_u32_text(text, pos, size, free * 4u);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static const char *vfs_proc_mount_kind_name(uint8_t kind) {
    if (kind == VFS_MOUNT_DEVFS) {
        return "devfs";
    }
    if (kind == VFS_MOUNT_PROCFS) {
        return "procfs";
    }
    if (kind == VFS_MOUNT_EVENTFS) {
        return "eventfs";
    }
    if (kind == VFS_MOUNT_FAT32) {
        return "fat32";
    }
    if (kind == VFS_MOUNT_NXFS) {
        return "nxfs";
    }
    return "unknown";
}

static uint32_t vfs_format_proc_mounts(struct vfs *vfs, char *text, uint32_t size) {
    uint32_t pos = 0;

    for (uint32_t i = 0; i < vfs_builtin_mount_count(vfs); i++) {
        struct vfs_mount_info info;
        uint32_t source_known = 0;

        if (!vfs_get_builtin_mount(vfs, i, &info, &source_known)) {
            continue;
        }
        (void)source_known;
        pos = vfs_append_text(text, pos, size, vfs_proc_mount_kind_name(info.kind));
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, info.name);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    for (uint32_t i = 0; i < vfs_mount_count(vfs); i++) {
        struct vfs_mount_info info;

        if (vfs_get_mount(vfs, i, &info) != 0) {
            continue;
        }
        pos = vfs_append_text(text, pos, size, vfs_proc_mount_kind_name(info.kind));
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, info.name);
        pos = vfs_append_text(text, pos, size, " /dev/disk");
        pos = vfs_append_u32_text(text, pos, size, info.disk_index);
        if (info.part_index != VFS_PARTITION_RAW) {
            pos = vfs_append_text(text, pos, size, "p");
            pos = vfs_append_u32_text(text, pos, size, info.part_index + 1u);
        }
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_uptime(char *text, uint32_t size) {
    uint32_t ticks = sched_current_ticks();
    uint32_t pos = 0;

    pos = vfs_append_u32_text(text, pos, size, ticks);
    pos = vfs_append_text(text, pos, size, " ticks\n");
    return pos;
}

static uint32_t vfs_format_proc_rtc(char *text, uint32_t size) {
    struct cmos_rtc_info rtc;
    uint32_t pos = 0;

    if (!cmos_rtc_query(&rtc) || !rtc.present) {
        return vfs_append_text(text, pos, size, "present: 0\n");
    }
    pos = vfs_append_text(text, pos, size, "present: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.present);
    pos = vfs_append_text(text, pos, size, "\nvalid: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.valid);
    pos = vfs_append_text(text, pos, size, "\nupdating: ");
    pos = vfs_append_bool_text(text, pos, size, rtc.updating);
    pos = vfs_append_text(text, pos, size, "\ntime: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.year);
    pos = vfs_append_text(text, pos, size, "-");
    pos = vfs_append_padded2_text(text, pos, size, rtc.month);
    pos = vfs_append_text(text, pos, size, "-");
    pos = vfs_append_padded2_text(text, pos, size, rtc.day);
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_padded2_text(text, pos, size, rtc.hour);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_padded2_text(text, pos, size, rtc.minute);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_padded2_text(text, pos, size, rtc.second);
    pos = vfs_append_text(text, pos, size, "\nweekday: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.weekday);
    pos = vfs_append_text(text, pos, size, "\nunix_time: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.unix_time);
    pos = vfs_append_text(text, pos, size, "\nmode: ");
    pos = vfs_append_text(text, pos, size, rtc.binary_mode ? "binary" : "bcd");
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_text(text, pos, size, rtc.hour_24 ? "24h" : "12h");
    pos = vfs_append_text(text, pos, size, "\nstatus_a: ");
    pos = vfs_append_hex_u32_text(text, pos, size, rtc.status_a);
    pos = vfs_append_text(text, pos, size, "\nstatus_b: ");
    pos = vfs_append_hex_u32_text(text, pos, size, rtc.status_b);
    pos = vfs_append_text(text, pos, size, "\ncentury: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.century);
    pos = vfs_append_text(text, pos, size, "\nraw_year: ");
    pos = vfs_append_u32_text(text, pos, size, rtc.raw_year);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_actions(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 18u);
    pos = vfs_append_padded_text(text, pos, size, "group", 10u);
    pos = vfs_append_padded_text(text, pos, size, "command", 10u);
    pos = vfs_append_padded_text(text, pos, size, "input", 16u);
    pos = vfs_append_padded_text(text, pos, size, "output", 20u);
    pos = vfs_append_padded_text(text, pos, size, "cap_flags", 12u);
    pos = vfs_append_padded_text(text, pos, size, "caps", 20u);
    pos = vfs_append_text(text, pos, size, "summary\n");
    for (uint32_t i = 0; i < vfs_proc_action_count(); i++) {
        const struct vfs_proc_action_entry *action = vfs_proc_action_at(i);

        if (action == 0) {
            continue;
        }
        pos = vfs_append_padded_text(text, pos, size, action->name, 18u);
        pos = vfs_append_padded_text(text, pos, size, action->group, 10u);
        pos = vfs_append_padded_text(text, pos, size, action->command, 10u);
        pos = vfs_append_padded_text(text, pos, size, action->input_schema, 16u);
        pos = vfs_append_padded_text(text, pos, size, action->output_schema, 20u);
        pos = vfs_append_hex_u32_text(text, pos, size, action->cap_flags);
        pos = vfs_append_padded_text(text, pos, size, "", 2u);
        pos = vfs_append_padded_text(text, pos, size, action->caps, 20u);
        pos = vfs_append_text(text, pos, size, action->summary);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_caps(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "flag", 12u);
    pos = vfs_append_padded_text(text, pos, size, "name", 18u);
    pos = vfs_append_text(text, pos, size, "summary\n");
    for (uint32_t i = 0; i < vfs_proc_cap_count(); i++) {
        const struct vfs_proc_cap_entry *cap = vfs_proc_cap_at(i);

        if (cap == 0) {
            continue;
        }
        pos = vfs_append_hex_u32_text(text, pos, size, cap->flag);
        pos = vfs_append_padded_text(text, pos, size, "", 2u);
        pos = vfs_append_padded_text(text, pos, size, cap->name, 18u);
        pos = vfs_append_text(text, pos, size, cap->summary);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_device_line(char *text,
                                            uint32_t pos,
                                            uint32_t size,
                                            const char *name,
                                            uint32_t major,
                                            uint32_t minor,
                                            const char *perm,
                                            const char *caps,
                                            const char *summary) {
    pos = vfs_append_padded_text(text, pos, size, name, 14u);
    pos = vfs_append_u32_text(text, pos, size, major);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_u32_text(text, pos, size, minor);
    pos = vfs_append_padded_text(text, pos, size, "", 8u);
    pos = vfs_append_padded_text(text, pos, size, perm, 6u);
    pos = vfs_append_padded_text(text, pos, size, caps, 28u);
    pos = vfs_append_text(text, pos, size, summary);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_devices(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "node", 14u);
    pos = vfs_append_padded_text(text, pos, size, "dev", 10u);
    pos = vfs_append_padded_text(text, pos, size, "perm", 6u);
    pos = vfs_append_padded_text(text, pos, size, "caps", 28u);
    pos = vfs_append_text(text, pos, size, "summary\n");
    pos = vfs_format_proc_device_line(text, pos, size, "tty", VFS_DEV_MAJOR_TTY, 0u, "rw", "device.read device.write", "console tty");
    pos = vfs_format_proc_device_line(text, pos, size, "tty2", VFS_DEV_MAJOR_TTY, 1u, "rw", "device.read device.write", "virtual console tty2");
    pos = vfs_format_proc_device_line(text, pos, size, "tty3", VFS_DEV_MAJOR_TTY, 2u, "rw", "device.read device.write", "virtual console tty3");
    pos = vfs_format_proc_device_line(text, pos, size, "null", VFS_DEV_MAJOR_MISC, 3u, "-w", "device.write", "discard writes");
    pos = vfs_format_proc_device_line(text, pos, size, "zero", VFS_DEV_MAJOR_MISC, 5u, "rw", "device.read device.write", "zero source and sink");
    pos = vfs_format_proc_device_line(text, pos, size, "stdin", VFS_DEV_MAJOR_MISC, 0u, "r-", "device.read", "standard input");
    pos = vfs_format_proc_device_line(text, pos, size, "stdout", VFS_DEV_MAJOR_MISC, 1u, "-w", "device.write", "standard output");
    pos = vfs_format_proc_device_line(text, pos, size, "stderr", VFS_DEV_MAJOR_MISC, 2u, "-w", "device.write", "standard error");
    pos = vfs_format_proc_device_line(text, pos, size, "ttyS0", VFS_DEV_MAJOR_TTY, 64u, "rw", "device.read device.write", "COM1 UART tty");
    if (framebuffer_display_active()) {
        pos = vfs_format_proc_device_line(text,
                                          pos,
                                          size,
                                          "fb",
                                          VFS_DEV_MAJOR_FRAMEBUFFER,
                                          0u,
                                          "rw",
                                          "device.read device.write",
                                          "framebuffer");
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);
        struct blockdev_info info;
        char name[16];
        const char *perm;
        const char *caps;

        if (dev == 0 || blockdev_get_info(disk_index, &info) != 0) {
            continue;
        }
        name[0] = '\0';
        vfs_format_disk_node_name(name, sizeof(name), disk_index);
        perm = info.writable ? "rw" : "r-";
        caps = info.writable ? "device.read device.write" : "device.read";
        pos = vfs_format_proc_device_line(text,
                                          pos,
                                          size,
                                          name,
                                          VFS_DEV_MAJOR_BLOCK,
                                          disk_index * 16u,
                                          perm,
                                          caps,
                                          info.name != 0 ? info.name : "block device");
        for (uint32_t part_index = 0; part_index < blockdev_partition_count(dev); part_index++) {
            struct blockdev_partition part;

            if (blockdev_partition_get(dev, part_index, &part) != 0) {
                continue;
            }
            name[0] = '\0';
            vfs_format_partition_node_name(name, sizeof(name), disk_index, part_index);
            pos = vfs_format_proc_device_line(text,
                                              pos,
                                              size,
                                              name,
                                              VFS_DEV_MAJOR_BLOCK,
                                              disk_index * 16u + part.index + 1u,
                                              perm,
                                              caps,
                                              "block partition");
        }
    }
    return pos;
}

static const char *vfs_proc_driver_kind_name(enum kernel_driver_kind kind) {
    switch (kind) {
        case KERNEL_DRIVER_KIND_STORAGE:
            return "storage";
        case KERNEL_DRIVER_KIND_USB:
            return "usb";
        case KERNEL_DRIVER_KIND_AUDIO:
            return "audio";
        case KERNEL_DRIVER_KIND_NET:
            return "net";
        default:
            return "unknown";
    }
}

static const char *vfs_proc_driver_state_name(enum kernel_driver_state state) {
    switch (state) {
        case KERNEL_DRIVER_STATE_REGISTERED:
            return "registered";
        case KERNEL_DRIVER_STATE_ACTIVE:
            return "active";
        case KERNEL_DRIVER_STATE_INACTIVE:
            return "inactive";
        case KERNEL_DRIVER_STATE_FAILED:
            return "failed";
        default:
            return "empty";
    }
}

static const char *vfs_proc_driver_file_state_name(enum kernel_driver_file_state state) {
    switch (state) {
        case KERNEL_DRIVER_FILE_ELF_INVALID:
            return "elf-invalid";
        case KERNEL_DRIVER_FILE_ELF_RELOC:
            return "elf-reloc";
        case KERNEL_DRIVER_FILE_LOADED:
            return "loaded";
        case KERNEL_DRIVER_FILE_LOAD_FAILED:
            return "load-failed";
        default:
            return "discovered";
    }
}

static uint32_t vfs_format_proc_drivers(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 14u);
    pos = vfs_append_padded_text(text, pos, size, "kind", 10u);
    pos = vfs_append_padded_text(text, pos, size, "state", 12u);
    pos = vfs_append_padded_text(text, pos, size, "result", 8u);
    pos = vfs_append_padded_text(text, pos, size, "source", 10u);
    pos = vfs_append_text(text, pos, size, "path\n");

    for (uint32_t i = 0; i < driver_count(); i++) {
        const struct kernel_driver_record *record = driver_get(i);

        if (record == 0 || record->driver == 0) {
            continue;
        }
        pos = vfs_append_padded_text(text, pos, size, record->driver->name, 14u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_kind_name(record->driver->kind),
                                     10u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_state_name(record->state),
                                     12u);
        pos = vfs_append_i32_text(text, pos, size, record->init_result);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     record->source != 0 ? record->source : "builtin",
                                     10u);
        pos = vfs_append_text(text, pos, size, record->path != 0 ? record->path : "-");
        pos = vfs_append_text(text, pos, size, "\n");
    }

    for (uint32_t i = 0; i < driver_file_count(); i++) {
        const struct kernel_driver_file *file = driver_get_file(i);

        if (file == 0) {
            continue;
        }
        pos = vfs_append_padded_text(text, pos, size, file->name, 14u);
        pos = vfs_append_padded_text(text, pos, size, "file", 10u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_file_state_name(file->state),
                                     12u);
        pos = vfs_append_u32_text(text, pos, size, file->size);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_padded_text(text, pos, size, "ramdisk", 10u);
        pos = vfs_append_text(text, pos, size, file->path);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_pid_status(uint32_t pid, char *text, uint32_t size) {
    struct process_snapshot proc;
    uint32_t pos = 0;

    if (!vfs_process_get_pid(pid, &proc)) {
        return 0;
    }
    pos = vfs_append_text(text, pos, size, "Name:\t");
    pos = vfs_append_text(text, pos, size, proc.name[0] != '\0' ? proc.name : "(unnamed)");
    pos = vfs_append_text(text, pos, size, "\nPid:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.pid);
    pos = vfs_append_text(text, pos, size, "\nSlot:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.slot);
    pos = vfs_append_text(text, pos, size, "\nState:\t");
    pos = vfs_append_text(text, pos, size, vfs_process_state_name(proc.state));
    pos = vfs_append_text(text, pos, size, "\nExitCode:\t");
    pos = vfs_append_i32_text(text, pos, size, proc.exit_code);
    pos = vfs_append_text(text, pos, size, "\nWakeTick:\t");
    pos = vfs_append_u32_text(text, pos, size, proc.wake_tick);
    pos = vfs_append_text(text, pos, size, "\nImage:\t");
    pos = vfs_append_text(text, pos, size, proc.image_kind == PROCESS_IMAGE_ELF ? "elf" : "none");
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

uint32_t vfs_format_procfs_node(struct vfs *vfs, struct vfs_node *node, char *text, uint32_t size) {
    if (vfs == 0 || node == 0 || text == 0) {
        return 0;
    }
    if (node->aux_index == VFS_PROC_MEMINFO) {
        return vfs_format_proc_meminfo(text, size);
    }
    if (node->aux_index == VFS_PROC_MOUNTS) {
        return vfs_format_proc_mounts(vfs, text, size);
    }
    if (node->aux_index == VFS_PROC_UPTIME) {
        return vfs_format_proc_uptime(text, size);
    }
    if (node->aux_index == VFS_PROC_RTC) {
        return vfs_format_proc_rtc(text, size);
    }
    if (node->aux_index == VFS_PROC_ACTIONS) {
        return vfs_format_proc_actions(text, size);
    }
    if (node->aux_index == VFS_PROC_CAPS) {
        return vfs_format_proc_caps(text, size);
    }
    if (node->aux_index == VFS_PROC_DEVICES) {
        return vfs_format_proc_devices(text, size);
    }
    if (node->aux_index == VFS_PROC_DRIVERS) {
        return vfs_format_proc_drivers(text, size);
    }
    if (node->aux_index == VFS_PROC_PID_STATUS) {
        return vfs_format_proc_pid_status(node->aux_data, text, size);
    }
    return 0;
}

int vfs_procfs_pid_exists(uint32_t pid) {
    return vfs_process_get_pid(pid, 0);
}
