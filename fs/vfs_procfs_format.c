#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "abi/syscall_abi.h"
#include "block/blockdev.h"
#include "drivers/audio/audio.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "drivers/serial/uart.h"
#include "drivers/video/framebuffer.h"
#include "drivers/rtc/cmos.h"
#include "kernel/internal/core/boot_state_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/public/driver/driver.h"
#include "hal/hal.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"

extern void syscall_compat32_vm_snapshot(struct syscall_vm_info *info)
    __attribute__((weak));

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
    struct syscall_vm_info vm;

    for (uint32_t i = 0u; i < sizeof(vm); i++) {
        ((uint8_t *)&vm)[i] = 0u;
    }
    vm.user_stack_pages = NOS_USER_STACK_SIZE / NOS_PAGE_SIZE;
    if (syscall_compat32_vm_snapshot != 0) {
        syscall_compat32_vm_snapshot(&vm);
    }

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
    pos = vfs_append_text(text, pos, size, "\nVmMmapRegions: ");
    pos = vfs_append_u32_text(text, pos, size, vm.mmap_regions);
    pos = vfs_append_text(text, pos, size, "\nVmMmapRegionCapacity: ");
    pos = vfs_append_u32_text(text, pos, size, vm.mmap_region_capacity);
    pos = vfs_append_text(text, pos, size, "\nVmMmapPages: ");
    pos = vfs_append_u32_text(text, pos, size, vm.mmap_pages);
    pos = vfs_append_text(text, pos, size, "\nVmSharedRegions: ");
    pos = vfs_append_u32_text(text, pos, size, vm.shared_regions);
    pos = vfs_append_text(text, pos, size, "\nVmShmObjects: ");
    pos = vfs_append_u32_text(text, pos, size, vm.shm_objects);
    pos = vfs_append_text(text, pos, size, "\nVmShmMappedPages: ");
    pos = vfs_append_u32_text(text, pos, size, vm.shm_mapped_pages);
    pos = vfs_append_text(text, pos, size, "\nVmUserStackPages: ");
    pos = vfs_append_u32_text(text, pos, size, vm.user_stack_pages);
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

    pos = vfs_append_padded_text(text, pos, size, "fs", 10u);
    pos = vfs_append_padded_text(text, pos, size, "target", 14u);
    pos = vfs_append_padded_text(text, pos, size, "source", 18u);
    pos = vfs_append_padded_text(text, pos, size, "slot", 8u);
    pos = vfs_append_padded_text(text, pos, size, "root", 6u);
    pos = vfs_append_text(text, pos, size, "ready\n");
    for (uint32_t i = 0; i < vfs_builtin_mount_count(vfs); i++) {
        struct vfs_mount_info info;
        uint32_t source_known = 0;

        if (!vfs_get_builtin_mount(vfs, i, &info, &source_known)) {
            continue;
        }
        pos = vfs_append_padded_text(text, pos, size, vfs_proc_mount_kind_name(info.kind), 10u);
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, info.name);
        pos = vfs_append_padded_text(text, pos, size, "", 14u);
        if (source_known) {
            pos = vfs_append_text(text, pos, size, "/dev/disk");
            pos = vfs_append_u32_text(text, pos, size, info.disk_index);
            if (info.part_index != VFS_PARTITION_RAW) {
                pos = vfs_append_text(text, pos, size, "p");
                pos = vfs_append_u32_text(text, pos, size, info.part_index + 1u);
            }
        } else {
            pos = vfs_append_text(text, pos, size, "pseudo");
        }
        pos = vfs_append_padded_text(text, pos, size, "", 18u);
        pos = vfs_append_text(text, pos, size, "builtin");
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_bool_text(text, pos, size, vfs != 0 && vfs->root_kind == info.kind && vfs->root_slot == 0u);
        pos = vfs_append_padded_text(text, pos, size, "", 6u);
        pos = vfs_append_bool_text(text, pos, size, vfs_mount_ready(vfs, info.kind));
        pos = vfs_append_text(text, pos, size, "\n");
    }
    if (vfs == 0) {
        return pos;
    }
    for (uint32_t slot = 0; slot < VFS_MOUNT_SLOT_MAX; slot++) {
        const uint32_t mount_slot = slot + 1u;
        const struct blockdev_partition *part = 0;

        if (!vfs->mounts[slot].used) {
            continue;
        }
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_mount_kind_name(vfs->mounts[slot].kind),
                                     10u);
        pos = vfs_append_text(text, pos, size, " /");
        pos = vfs_append_text(text, pos, size, vfs->mounts[slot].name);
        pos = vfs_append_padded_text(text, pos, size, "", 14u);
        pos = vfs_append_text(text, pos, size, " /dev/disk");
        pos = vfs_append_u32_text(text, pos, size, vfs->mounts[slot].disk_index);
        if (vfs->mounts[slot].part_index != VFS_PARTITION_RAW) {
            pos = vfs_append_text(text, pos, size, "p");
            pos = vfs_append_u32_text(text, pos, size, vfs->mounts[slot].part_index + 1u);
        }
        pos = vfs_append_padded_text(text, pos, size, "", 18u);
        pos = vfs_append_u32_text(text, pos, size, mount_slot);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_bool_text(text,
                                   pos,
                                   size,
                                   vfs->root_kind == vfs->mounts[slot].kind &&
                                       vfs->root_slot == mount_slot);
        pos = vfs_append_padded_text(text, pos, size, "", 6u);
        pos = vfs_append_bool_text(text, pos, size, vfs_mount_ready(vfs, vfs->mounts[slot].kind));
        if (vfs->mounts[slot].part_index != VFS_PARTITION_RAW) {
            struct block_device *dev = blockdev_get(vfs->mounts[slot].disk_index);

            if (dev != 0 && vfs->mounts[slot].part_index < blockdev_partition_count(dev)) {
                part = &dev->partitions[vfs->mounts[slot].part_index];
            }
        }
        if (part != 0) {
            pos = vfs_append_text(text, pos, size, " lba=");
            pos = vfs_append_u64_text(text, pos, size, part->start_lba);
            pos = vfs_append_text(text, pos, size, " sectors=");
            pos = vfs_append_u64_text(text, pos, size, part->sector_count);
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
                                            const char *active,
                                            const char *perm,
                                            const char *caps,
                                            const char *summary) {
    pos = vfs_append_padded_text(text, pos, size, name, 14u);
    pos = vfs_append_u32_text(text, pos, size, major);
    pos = vfs_append_text(text, pos, size, ":");
    pos = vfs_append_u32_text(text, pos, size, minor);
    pos = vfs_append_padded_text(text, pos, size, "", 8u);
    pos = vfs_append_padded_text(text, pos, size, active, 8u);
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
    pos = vfs_append_padded_text(text, pos, size, "active", 8u);
    pos = vfs_append_padded_text(text, pos, size, "perm", 6u);
    pos = vfs_append_padded_text(text, pos, size, "caps", 28u);
    pos = vfs_append_text(text, pos, size, "summary\n");
    pos = vfs_format_proc_device_line(text, pos, size, "tty", VFS_DEV_MAJOR_TTY, 0u, "yes", "rw", "device.read device.write", "console tty");
    pos = vfs_format_proc_device_line(text, pos, size, "tty2", VFS_DEV_MAJOR_TTY, 1u, "yes", "rw", "device.read device.write", "virtual console tty2");
    pos = vfs_format_proc_device_line(text, pos, size, "tty3", VFS_DEV_MAJOR_TTY, 2u, "yes", "rw", "device.read device.write", "virtual console tty3");
    pos = vfs_format_proc_device_line(text, pos, size, "null", VFS_DEV_MAJOR_MISC, 3u, "yes", "-w", "device.write", "discard writes");
    pos = vfs_format_proc_device_line(text, pos, size, "zero", VFS_DEV_MAJOR_MISC, 5u, "yes", "rw", "device.read device.write", "zero source and sink");
    pos = vfs_format_proc_device_line(text, pos, size, "stdin", VFS_DEV_MAJOR_MISC, 0u, "yes", "r-", "device.read", "standard input");
    pos = vfs_format_proc_device_line(text, pos, size, "stdout", VFS_DEV_MAJOR_MISC, 1u, "yes", "-w", "device.write", "standard output");
    pos = vfs_format_proc_device_line(text, pos, size, "stderr", VFS_DEV_MAJOR_MISC, 2u, "yes", "-w", "device.write", "standard error");
    if (audio_device_count() != 0u) {
        pos = vfs_format_proc_device_line(text,
                                          pos,
                                          size,
                                          "audio",
                                          VFS_DEV_MAJOR_AUDIO,
                                          0u,
                                          audio_default_output_device(0) ? "yes" : "no",
                                          "-w",
                                          "device.write audio.play",
                                          "48 kHz 16-bit stereo PCM output");
    }
    pos = vfs_format_proc_device_line(text,
                                      pos,
                                      size,
                                      "speaker",
                                      VFS_DEV_MAJOR_AUDIO,
                                      1u,
                                      "yes",
                                      "-w",
                                      "device.write",
                                      "PC speaker tone: write HZ DURATION_MS");
    if (uart_is_ready()) {
        pos = vfs_format_proc_device_line(text, pos, size, "ttyS0", VFS_DEV_MAJOR_TTY, 64u, "yes", "rw", "device.read device.write", "COM1 UART tty");
    }
    if (framebuffer_display_active()) {
        pos = vfs_format_proc_device_line(text,
                                          pos,
                                          size,
                                          "fb",
                                          VFS_DEV_MAJOR_FRAMEBUFFER,
                                          0u,
                                          "yes",
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
                                          "yes",
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
                                              "yes",
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

static const char *vfs_proc_driver_reason_name(enum kernel_driver_reason reason) {
    switch (reason) {
        case KERNEL_DRIVER_REASON_EMPTY:
            return "empty";
        case KERNEL_DRIVER_REASON_REGISTERED:
            return "registered";
        case KERNEL_DRIVER_REASON_PROBE_OK:
            return "probe-ok";
        case KERNEL_DRIVER_REASON_UNSUPPORTED_ELF:
            return "unsupported-elf";
        case KERNEL_DRIVER_REASON_LOADED:
            return "loaded";
        case KERNEL_DRIVER_REASON_LOAD_FAILED:
            return "load-failed";
        case KERNEL_DRIVER_REASON_INIT_OK:
            return "init-ok";
        case KERNEL_DRIVER_REASON_MISSING_HARDWARE:
            return "missing-hardware";
        case KERNEL_DRIVER_REASON_INIT_FAILED:
            return "init-failed";
        case KERNEL_DRIVER_REASON_REGISTER_FAILED:
            return "register-failed";
        case KERNEL_DRIVER_REASON_LAYOUT_FAILED:
            return "layout-failed";
        case KERNEL_DRIVER_REASON_RELOC_FAILED:
            return "reloc-failed";
        case KERNEL_DRIVER_REASON_SYMBOL_MISSING:
            return "symbol-missing";
        case KERNEL_DRIVER_REASON_NO_MEMORY:
            return "no-memory";
        default:
            return 0;
    }
}

static const char *vfs_proc_driver_file_class_name(uint8_t elf_class) {
    if (elf_class == 1u) {
        return "ELF32";
    }
    if (elf_class == 2u) {
        return "ELF64";
    }
    return "unknown";
}

static const char *vfs_proc_driver_file_machine_name(uint16_t machine) {
    if (machine == 3u) {
        return "i386";
    }
    if (machine == 62u) {
        return "x86_64";
    }
    if (machine == 0u) {
        return "unknown";
    }
    return "machine";
}

static const char *vfs_proc_driver_file_type_name(uint16_t type) {
    if (type == 1u) {
        return "REL";
    }
    if (type == 2u) {
        return "EXEC";
    }
    if (type == 3u) {
        return "DYN";
    }
    if (type == 0u) {
        return "unknown";
    }
    return "type";
}

static const char *vfs_proc_driver_file_source_name(const char *path) {
    if (path == 0) {
        return "unknown";
    }
    if (path[0] == '/' && path[1] == 'r' && path[2] == 'a' && path[3] == 'm' &&
        path[4] == '/') {
        return "ramdisk";
    }
    if (path[0] == '/' && path[1] == 'D' && path[2] == 'R' && path[3] == 'I' &&
        path[4] == 'V' && path[5] == 'E' && path[6] == 'R' && path[7] == 'S' &&
        path[8] == '/') {
        return "rootfs";
    }
    return "vfs";
}

static uint32_t vfs_append_driver_file_elf_text(char *text,
                                                uint32_t pos,
                                                uint32_t size,
                                                const struct kernel_driver_file *file) {
    if (file == 0 || file->elf_class == 0u) {
        return vfs_append_text(text, pos, size, "-");
    }
    pos = vfs_append_text(text,
                          pos,
                          size,
                          vfs_proc_driver_file_class_name(file->elf_class));
    pos = vfs_append_text(text, pos, size, "/");
    pos = vfs_append_text(text,
                          pos,
                          size,
                          vfs_proc_driver_file_machine_name(file->elf_machine));
    pos = vfs_append_text(text, pos, size, "/");
    pos = vfs_append_text(text,
                          pos,
                          size,
                          vfs_proc_driver_file_type_name(file->elf_type));
    if (file->elf_data != 1u) {
        pos = vfs_append_text(text, pos, size, "/data=");
        pos = vfs_append_u32_text(text, pos, size, file->elf_data);
    }
    return pos;
}

static uint32_t vfs_format_proc_drivers(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 14u);
    pos = vfs_append_padded_text(text, pos, size, "kind", 10u);
    pos = vfs_append_padded_text(text, pos, size, "driver", 14u);
    pos = vfs_append_padded_text(text, pos, size, "state", 12u);
    pos = vfs_append_padded_text(text, pos, size, "result", 8u);
    pos = vfs_append_padded_text(text, pos, size, "source", 10u);
    pos = vfs_append_padded_text(text, pos, size, "elf", 18u);
    pos = vfs_append_padded_text(text, pos, size, "reason", 18u);
    pos = vfs_append_padded_text(text, pos, size, "device", 20u);
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
        pos = vfs_append_padded_text(text, pos, size, record->driver->name, 14u);
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
        pos = vfs_append_padded_text(text, pos, size, "-", 18u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_reason_name(record->reason_code) != 0
                                         ? vfs_proc_driver_reason_name(record->reason_code)
                                         : (record->reason != 0 ? record->reason : "-"),
                                     18u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     record->device[0] != '\0' ? record->device : "-",
                                     20u);
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
                                     file->driver_name[0] != '\0' ? file->driver_name : "-",
                                     14u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_file_state_name(file->state),
                                     12u);
        pos = vfs_append_u32_text(text, pos, size, file->size);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_file_source_name(file->path),
                                     10u);
        pos = vfs_append_driver_file_elf_text(text, pos, size, file);
        pos = vfs_append_padded_text(text, pos, size, "", 18u);
        pos = vfs_append_padded_text(text,
                                     pos,
                                     size,
                                     vfs_proc_driver_reason_name(file->reason_code) != 0
                                         ? vfs_proc_driver_reason_name(file->reason_code)
                                         : (file->reason != 0 ? file->reason : "-"),
                                     18u);
        pos = vfs_append_padded_text(text, pos, size, "-", 20u);
        pos = vfs_append_text(text, pos, size, file->path);
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_cpuinfo(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_text(text, pos, size, "processor\t: 0\n");
#if defined(__x86_64__)
    pos = vfs_append_text(text, pos, size, "arch\t\t: x86_64\n");
    pos = vfs_append_text(text, pos, size, "bits\t\t: 64\n");
#elif defined(__i386__)
    pos = vfs_append_text(text, pos, size, "arch\t\t: i386\n");
    pos = vfs_append_text(text, pos, size, "bits\t\t: 32\n");
#else
    pos = vfs_append_text(text, pos, size, "arch\t\t: generic\n");
    pos = vfs_append_text(text, pos, size, "bits\t\t: 0\n");
#endif
    pos = vfs_append_text(text, pos, size, "vendor_id\t: NexOS\n");
    pos = vfs_append_text(text, pos, size, "model name\t: NexOS virtual CPU\n");
    pos = vfs_append_text(text, pos, size, "features\t: protected-mode paging syscall\n");
    return pos;
}

static uint32_t vfs_format_proc_filesystems(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_text(text, pos, size, "nodev\tdevfs\n");
    pos = vfs_append_text(text, pos, size, "nodev\tprocfs\n");
    pos = vfs_append_text(text, pos, size, "nodev\teventfs\n");
    pos = vfs_append_text(text, pos, size, "\tfat32\n");
    pos = vfs_append_text(text, pos, size, "\tnxfs\n");
    return pos;
}

static uint32_t vfs_format_proc_block(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 14u);
    pos = vfs_append_padded_text(text, pos, size, "blocks", 14u);
    pos = vfs_append_padded_text(text, pos, size, "block_size", 12u);
    pos = vfs_append_padded_text(text, pos, size, "parts", 8u);
    pos = vfs_append_text(text, pos, size, "writable\n");
    for (uint32_t i = 0; i < blockdev_count(); i++) {
        struct block_device *dev = blockdev_get(i);
        struct blockdev_info info;
        char name[16];

        if (dev == 0 || blockdev_get_info(i, &info) != 0) {
            continue;
        }
        name[0] = '\0';
        vfs_format_disk_node_name(name, sizeof(name), i);
        pos = vfs_append_padded_text(text, pos, size, name, 14u);
        pos = vfs_append_u64_text(text, pos, size, info.block_count);
        pos = vfs_append_padded_text(text, pos, size, "", 14u);
        pos = vfs_append_u32_text(text, pos, size, info.block_size);
        pos = vfs_append_padded_text(text, pos, size, "", 12u);
        pos = vfs_append_u32_text(text, pos, size, blockdev_partition_count(dev));
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_bool_text(text, pos, size, info.writable);
        pos = vfs_append_text(text, pos, size, " ");
        pos = vfs_append_text(text, pos, size, info.name != 0 ? info.name : "block device");
        pos = vfs_append_text(text, pos, size, "\n");
    }
    return pos;
}

static uint32_t vfs_format_proc_partitions(char *text, uint32_t size) {
    uint32_t pos = 0;

    pos = vfs_append_padded_text(text, pos, size, "name", 14u);
    pos = vfs_append_padded_text(text, pos, size, "type", 8u);
    pos = vfs_append_padded_text(text, pos, size, "start", 14u);
    pos = vfs_append_padded_text(text, pos, size, "sectors", 14u);
    pos = vfs_append_text(text, pos, size, "boot\n");
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0) {
            continue;
        }
        for (uint32_t part_index = 0; part_index < blockdev_partition_count(dev); part_index++) {
            struct blockdev_partition part;
            char name[16];

            if (blockdev_partition_get(dev, part_index, &part) != 0) {
                continue;
            }
            name[0] = '\0';
            vfs_format_partition_node_name(name, sizeof(name), disk_index, part_index);
            pos = vfs_append_padded_text(text, pos, size, name, 14u);
            pos = vfs_append_hex_u32_text(text, pos, size, part.type);
            pos = vfs_append_padded_text(text, pos, size, "", 8u);
            pos = vfs_append_u64_text(text, pos, size, part.start_lba);
            pos = vfs_append_padded_text(text, pos, size, "", 14u);
            pos = vfs_append_u64_text(text, pos, size, part.sector_count);
            pos = vfs_append_padded_text(text, pos, size, "", 14u);
            pos = vfs_append_text(text, pos, size, part.bootable ? "yes" : "no");
            pos = vfs_append_text(text, pos, size, "\n");
        }
    }
    return pos;
}

static uint32_t vfs_format_proc_cmdline(char *text, uint32_t size) {
    const struct kernel_boot_state *state = kernel_boot_state_get();
    uint32_t pos = 0;

    pos = vfs_append_text(text, pos, size, state != 0 ? state->cmdline : "");
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_version(char *text, uint32_t size) {
    const struct kernel_boot_state *state = kernel_boot_state_get();
    uint32_t pos = 0;

    if (state == 0) {
        return vfs_append_text(text, pos, size, "NexOS kernel unknown unknown\n");
    }
    pos = vfs_append_text(text, pos, size, state->os_name);
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_text(text, pos, size, state->kernel_name);
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_text(text, pos, size, state->kernel_version);
    pos = vfs_append_text(text, pos, size, " ");
    pos = vfs_append_text(text, pos, size, state->arch_name);
    pos = vfs_append_text(text, pos, size, " build ");
    pos = vfs_append_text(text, pos, size, state->build_date);
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_fb(char *text, uint32_t size) {
    struct syscall_gfx_info info;
    uint32_t pos = 0;

    for (uint32_t i = 0u; i < sizeof(info); i++) {
        ((uint8_t *)&info)[i] = 0u;
    }
    kernel_gfx_info(&info);
    pos = vfs_append_text(text, pos, size, "active: ");
    pos = vfs_append_bool_text(text, pos, size, framebuffer_display_active() ? 1u : 0u);
    pos = vfs_append_text(text, pos, size, "\nwidth: ");
    pos = vfs_append_u32_text(text, pos, size, info.width);
    pos = vfs_append_text(text, pos, size, "\nheight: ");
    pos = vfs_append_u32_text(text, pos, size, info.height);
    pos = vfs_append_text(text, pos, size, "\npitch: ");
    pos = vfs_append_u32_text(text, pos, size, info.pitch);
    pos = vfs_append_text(text, pos, size, "\nbpp: ");
    pos = vfs_append_u32_text(text, pos, size, info.bpp);
    pos = vfs_append_text(text, pos, size, "\ntext_columns: ");
    pos = vfs_append_u32_text(text, pos, size, info.text_columns);
    pos = vfs_append_text(text, pos, size, "\ntext_rows: ");
    pos = vfs_append_u32_text(text, pos, size, info.text_rows);
    pos = vfs_append_text(text, pos, size, "\ncell_height: ");
    pos = vfs_append_u32_text(text, pos, size, hal_display_cell_height());
    pos = vfs_append_text(text, pos, size, "\ndevice_size: ");
    pos = vfs_append_u32_text(text, pos, size, framebuffer_device_size());
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static const char *vfs_proc_irq_name(uint32_t irq) {
    switch (irq) {
        case 0u:
            return "timer";
        case 1u:
            return "keyboard";
        case 4u:
            return "serial";
        case 12u:
            return "mouse";
        case 14u:
            return "ata-primary";
        case 15u:
            return "ata-secondary";
        default:
            return "-";
    }
}

static uint32_t vfs_format_proc_interrupts(char *text, uint32_t size) {
    struct kernel_irq_state state;
    uint32_t pos = 0;

    kernel_irq_state_snapshot(&state);
    pos = vfs_append_text(text, pos, size, "total: ");
    pos = vfs_append_u32_text(text, pos, size, state.total);
    pos = vfs_append_text(text, pos, size, "\nuser: ");
    pos = vfs_append_u32_text(text, pos, size, state.user);
    pos = vfs_append_text(text, pos, size, "\n\n");
    pos = vfs_append_padded_text(text, pos, size, "irq", 8u);
    pos = vfs_append_padded_text(text, pos, size, "count", 12u);
    pos = vfs_append_text(text, pos, size, "name\n");
    for (uint32_t irq = 0u; irq < 16u; irq++) {
        pos = vfs_append_u32_text(text, pos, size, irq);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_u32_text(text, pos, size, state.lines[irq]);
        pos = vfs_append_padded_text(text, pos, size, "", 12u);
        pos = vfs_append_text(text, pos, size, vfs_proc_irq_name(irq));
        pos = vfs_append_text(text, pos, size, "\n");
    }
    pos = vfs_append_text(text, pos, size, "\ntimer_ticks: ");
    pos = vfs_append_u32_text(text, pos, size, sched_current_ticks());
    pos = vfs_append_text(text, pos, size, "\nkeyboard_events: pending=");
    pos = vfs_append_u32_text(text, pos, size, keyboard_event_queue_pending());
    pos = vfs_append_text(text, pos, size, " dropped=");
    pos = vfs_append_u32_text(text, pos, size, keyboard_event_queue_dropped());
    pos = vfs_append_text(text, pos, size, " latest_seq=");
    pos = vfs_append_u32_text(text, pos, size, keyboard_event_queue_latest_seq());
    pos = vfs_append_text(text, pos, size, "\nmouse_events: pending=");
    pos = vfs_append_u32_text(text, pos, size, mouse_event_pending());
    pos = vfs_append_text(text, pos, size, " dropped=");
    pos = vfs_append_u32_text(text, pos, size, mouse_event_dropped());
    pos = vfs_append_text(text, pos, size, " latest_seq=");
    pos = vfs_append_u32_text(text, pos, size, mouse_event_latest_seq());
    pos = vfs_append_text(text, pos, size, "\n");
    return pos;
}

static uint32_t vfs_format_proc_tty(char *text, uint32_t size) {
    uint32_t pos = 0;
    uint32_t active = tty_active_index();

    pos = vfs_append_text(text, pos, size, "active: ");
    pos = vfs_append_u32_text(text, pos, size, active);
    pos = vfs_append_text(text, pos, size, "\ncolumns: ");
    pos = vfs_append_u32_text(text, pos, size, hal_display_text_columns());
    pos = vfs_append_text(text, pos, size, "\nrows: ");
    pos = vfs_append_u32_text(text, pos, size, hal_display_text_rows());
    pos = vfs_append_text(text, pos, size, "\n\n");
    pos = vfs_append_padded_text(text, pos, size, "tty", 8u);
    pos = vfs_append_padded_text(text, pos, size, "cursor", 14u);
    pos = vfs_append_padded_text(text, pos, size, "input", 8u);
    pos = vfs_append_padded_text(text, pos, size, "ready", 8u);
    pos = vfs_append_text(text, pos, size, "fg_pid\n");
    for (uint32_t i = 0u; i < TTY_VIRTUAL_COUNT; i++) {
        struct tty *tty = tty_virtual(i);

        if (tty == 0) {
            continue;
        }
        pos = vfs_append_u32_text(text, pos, size, i);
        pos = vfs_append_padded_text(text, pos, size, i == active ? "*" : "", 8u);
        pos = vfs_append_u32_text(text, pos, size, tty->console.cursor_row);
        pos = vfs_append_text(text, pos, size, ":");
        pos = vfs_append_u32_text(text, pos, size, tty->console.cursor_col);
        pos = vfs_append_padded_text(text, pos, size, "", 14u);
        pos = vfs_append_u32_text(text, pos, size, tty->input_len);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_bool_text(text, pos, size, tty->line_ready);
        pos = vfs_append_padded_text(text, pos, size, "", 8u);
        pos = vfs_append_u32_text(text, pos, size, tty_foreground_pid(tty));
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
    if (node->aux_index == VFS_PROC_CPUINFO) {
        return vfs_format_proc_cpuinfo(text, size);
    }
    if (node->aux_index == VFS_PROC_FILESYSTEMS) {
        return vfs_format_proc_filesystems(text, size);
    }
    if (node->aux_index == VFS_PROC_BLOCK) {
        return vfs_format_proc_block(text, size);
    }
    if (node->aux_index == VFS_PROC_PARTITIONS) {
        return vfs_format_proc_partitions(text, size);
    }
    if (node->aux_index == VFS_PROC_CMDLINE) {
        return vfs_format_proc_cmdline(text, size);
    }
    if (node->aux_index == VFS_PROC_VERSION) {
        return vfs_format_proc_version(text, size);
    }
    if (node->aux_index == VFS_PROC_FB) {
        return vfs_format_proc_fb(text, size);
    }
    if (node->aux_index == VFS_PROC_INTERRUPTS) {
        return vfs_format_proc_interrupts(text, size);
    }
    if (node->aux_index == VFS_PROC_TTY) {
        return vfs_format_proc_tty(text, size);
    }
    if (node->aux_index == VFS_PROC_PID_STATUS) {
        return vfs_format_proc_pid_status(node->aux_data, text, size);
    }
    return 0;
}

int vfs_procfs_pid_exists(uint32_t pid) {
    return vfs_process_get_pid(pid, 0);
}
