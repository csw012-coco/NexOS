#include "fs/vfs_internal.h"
#include "fs/vfs_text.h"
#include "drivers/rtc/cmos.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"
#include "lib/string.h"

static int64_t vfs_procfs_emit_dir_entry(struct vfs_dirent *entry,
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

int64_t vfs_read_from_procfs(struct vfs *vfs,
                             struct vfs_node *node,
                             uint32_t *offset_io,
                             void *buffer,
                             uint32_t size) {
    char *proc_text;
    uint32_t proc_text_size;
    uint32_t text_size = 0;

    if (vfs == 0 || node == 0 || offset_io == 0 || buffer == 0) {
        return -1;
    }
    if (node->aux_index == VFS_PROC_KMSG) {
        uint32_t copied = kprint_log_read(*offset_io, (char *)buffer, size);

        *offset_io += copied;
        return (int64_t)copied;
    }
    proc_text = vfs->procfs_text;
    proc_text_size = sizeof(vfs->procfs_text);
    proc_text[0] = '\0';
    if (node->aux_index == VFS_PROC_MEMINFO) {
        text_size = vfs_format_proc_meminfo(proc_text, proc_text_size);
    } else if (node->aux_index == VFS_PROC_MOUNTS) {
        text_size = vfs_format_proc_mounts(vfs, proc_text, proc_text_size);
    } else if (node->aux_index == VFS_PROC_UPTIME) {
        text_size = vfs_format_proc_uptime(proc_text, proc_text_size);
    } else if (node->aux_index == VFS_PROC_RTC) {
        text_size = vfs_format_proc_rtc(proc_text, proc_text_size);
    } else if (node->aux_index == VFS_PROC_ACTIONS) {
        text_size = vfs_format_proc_actions(proc_text, proc_text_size);
    } else if (node->aux_index == VFS_PROC_PID_STATUS) {
        text_size = vfs_format_proc_pid_status(node->aux_data, proc_text, proc_text_size);
    } else {
        return -1;
    }
    return vfs_read_from_generated_text(offset_io, buffer, size, proc_text, text_size);
}

int64_t vfs_read_dir_procfs(struct vfs_node *node, uint32_t *index_io, struct vfs_dirent *entry) {
    if (node->aux_index == VFS_PROC_PID_DIR) {
        if (*index_io == 0) {
            return vfs_procfs_emit_dir_entry(entry, index_io, "status", 0, 0);
        }
        return 0;
    }
    if (*index_io == 0) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "meminfo", 0, 0);
    }
    if (*index_io == 1) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "mounts", 0, 0);
    }
    if (*index_io == 2) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "uptime", 0, 0);
    }
    if (*index_io == 3) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "kmsg", 0, 0);
    }
    if (*index_io == 4) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "actions", 0, 0);
    }
    if (*index_io == 5) {
        return vfs_procfs_emit_dir_entry(entry, index_io, "rtc", 0, 0);
    }
    {
        uint32_t ordinal = *index_io - 6u;
        uint32_t seen = 0;

        for (uint32_t i = 0; i < process_capacity(); i++) {
            struct process_snapshot proc;
            char name[16];

            if (!process_get(i, &proc)) {
                continue;
            }
            if (seen == ordinal) {
                name[0] = '\0';
                vfs_append_u32_text(name, 0, sizeof(name), proc.pid);
                return vfs_procfs_emit_dir_entry(entry, index_io, name, 0, VFS_ATTR_DIR);
            }
            seen++;
        }
    }
    return 0;
}

int vfs_procfs_lookup(const char *name, struct vfs_node *out) {
    const char *slash;
    char pid_text[16];
    uint32_t pid_len = 0;
    uint32_t pid = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (streq(name, "meminfo")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_MEMINFO, 0);
        return 0;
    }
    if (streq(name, "mounts")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_MOUNTS, 0);
        return 0;
    }
    if (streq(name, "uptime")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_UPTIME, 0);
        return 0;
    }
    if (streq(name, "kmsg")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_KMSG, 0);
        return 0;
    }
    if (streq(name, "actions")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_ACTIONS, 0);
        return 0;
    }
    if (streq(name, "rtc")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_RTC, 0);
        return 0;
    }
    slash = name;
    while (*slash != '\0' && *slash != '/') {
        if (pid_len + 1u >= sizeof(pid_text)) {
            return -1;
        }
        pid_text[pid_len++] = *slash++;
    }
    pid_text[pid_len] = '\0';
    if (*slash != '/' || !vfs_parse_u32_full(pid_text, &pid) || !vfs_process_get_pid(pid, 0)) {
        return -1;
    }
    slash++;
    if (streq(slash, "status")) {
        vfs_set_procfs_node(out, VFS_NODE_FILE, VFS_PROC_PID_STATUS, pid);
        return 0;
    }
    return -1;
}

int vfs_procfs_opendir(const char *name, struct vfs_node *out) {
    uint32_t pid = 0;

    if (name == 0 || out == 0) {
        return -1;
    }
    if (!vfs_parse_u32_full(name, &pid) || !vfs_process_get_pid(pid, 0)) {
        return -1;
    }
    vfs_set_procfs_node(out, VFS_NODE_DIR, VFS_PROC_PID_DIR, pid);
    return 0;
}
